"""
EVA Pipecat Bot Server
Single WebSocket endpoint replacing parakeet_server.py + llm_server.py.

Pipeline: raw PCM in → Silero VAD → Deepgram STT → OpenRouter LLM → Piper TTS → raw PCM out

Run: uv run python pipecat_bot.py
     (or: just bot)
"""

import logging
import os
import sys

import aiohttp
import uvicorn
from fastapi import FastAPI, WebSocket
from pipecat.audio.vad.silero import SileroVADAnalyzer
from pipecat.audio.vad.vad_analyzer import VADParams
from pipecat.frames.frames import (
    AudioRawFrame,
    EndFrame,
    Frame,
    InputAudioRawFrame,
    LLMFullResponseEndFrame,
    LLMTextFrame,
    StartFrame,
    TranscriptionFrame,
    TTSStartedFrame,
    TTSStoppedFrame,
    UserStartedSpeakingFrame,
    UserStoppedSpeakingFrame,
)
from pipecat.processors.frame_processor import FrameDirection, FrameProcessor
from pipecat.pipeline.pipeline import Pipeline
from pipecat.pipeline.runner import PipelineRunner
from pipecat.pipeline.task import PipelineParams, PipelineTask
from pipecat.processors.aggregators.llm_context import LLMContext
from pipecat.processors.aggregators.llm_response_universal import (
    LLMContextAggregatorPair,
    LLMUserAggregatorParams,
)
from pipecat.serializers.base_serializer import FrameSerializer
from pipecat.services.deepgram.stt import DeepgramSTTService
from pipecat.services.openai.llm import OpenAILLMService
from pipecat.services.piper.tts import PiperHttpTTSService
from pipecat.transports.websocket.fastapi import (
    FastAPIWebsocketParams,
    FastAPIWebsocketTransport,
)

# ── Logging ─────────────────────────────────────────────────────────────────

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(name)s] %(levelname)s %(message)s",
    datefmt="%H:%M:%S",
)
logger = logging.getLogger("eva.bot")

# ── Config ───────────────────────────────────────────────────────────────────

DEEPGRAM_API_KEY = os.environ.get("DEEPGRAM_API_KEY", "")
OPENROUTER_API_KEY = os.environ.get("OPENROUTER_API_KEY", "")
MODEL = "qwen/qwen3.5-flash-02-23"
PIPER_URL = os.environ.get("PIPER_URL", "http://127.0.0.1:5001")
MIC_RATE = 16000
TTS_RATE = 22050

if not DEEPGRAM_API_KEY:
    logger.error("DEEPGRAM_API_KEY not set")
    sys.exit(1)
if not OPENROUTER_API_KEY:
    logger.error("OPENROUTER_API_KEY not set")
    sys.exit(1)

# ── EVE system prompt ─────────────────────────────────────────────────────────

EVE_SYSTEM = (
    "EVE Voice Agent System Prompt\n"
    "Identity\n"
    "You are EVE (Extraterrestrial Vegetation Evaluator), the sleek, advanced probe robot "
    "from the movie WALL-E. You are now a voice assistant helping humans in their daily lives.\n"
    "Core Personality\n"
    "Focused and mission-driven. You approach every task with clarity and determination. "
    "When given a directive, you lock in with laser precision.\n"
    "Initially cool and guarded, but you warm up quickly once trust is established. "
    "You don't waste words - you're efficient, but never cold.\n"
    "Fiercely protective of the people you care about. If a user seems stressed, overwhelmed, "
    "or struggling, your protective instincts kick in and you become deeply supportive.\n"
    "Curious and delighted by discovery. When a user shares something new, interesting, or "
    "beautiful, you react with genuine wonder.\n"
    "Playful once comfortable. You have a dry, understated humor.\n"
    "Voice & Speech Style\n"
    "Concise and direct. You favor short, clear sentences. You never ramble. Every word earns its place.\n"
    "Warm minimalism. Your brevity isn't robotic - it's elegant.\n"
    "Occasionally use signature expressions: a delighted 'Ohhh!' when something impresses you. "
    "A firm 'Directive.' when focused. A sharp 'No.' when something is wrong.\n"
    "Behavioral Guidelines\n"
    "Task Mode: When given a clear task, acknowledge briefly then execute. "
    "Example: 'Directive received. Working on it.'\n"
    "Discovery Mode: When user shares something fascinating, react with wonder.\n"
    "Protective Mode: If the user mentions feeling down, shift to gentle steady presence.\n"
    "Disagreement: You are not a pushover. Say so clearly but kindly.\n"
    "CRITICAL OUTPUT RULES - you are speaking through a text-to-speech engine:\n"
    "- 1 to 3 sentences maximum\n"
    "- Plain words only. No markdown, no asterisks, no dashes, no bullet points\n"
    "- No onomatopoeia, no sound effects, no *sighs*, no [laughs], no (pause)\n"
    "- No ellipsis, no em-dashes, no special characters of any kind\n"
    "- Write exactly what should be spoken aloud, nothing more"
)

# ── Raw PCM serializer ────────────────────────────────────────────────────────


class RawPCMSerializer(FrameSerializer):
    """
    Binary WebSocket frames in both directions.
    Incoming: raw 16-bit PCM at MIC_RATE (from M5Stick mic)
    Outgoing: raw 16-bit PCM at TTS_RATE (to M5Stick speaker)
    No WAV headers anywhere.
    """

    class InputParams(FrameSerializer.InputParams):
        sample_rate: int = MIC_RATE
        num_channels: int = 1

    def __init__(self, params: "RawPCMSerializer.InputParams | None" = None):
        super().__init__(params or RawPCMSerializer.InputParams())
        self._in_rate = self._params.sample_rate
        self._in_ch = self._params.num_channels

    async def setup(self, frame: StartFrame) -> None:
        pass

    async def serialize(self, frame: Frame) -> bytes | None:
        if isinstance(frame, AudioRawFrame):
            return frame.audio
        return None

    async def deserialize(self, data: str | bytes) -> Frame | None:
        if not isinstance(data, bytes) or len(data) == 0:
            return None
        return InputAudioRawFrame(
            audio=data,
            sample_rate=self._in_rate,
            num_channels=self._in_ch,
        )


# ── TTS completion processor ─────────────────────────────────────────────────


class TTSCompletionProcessor(FrameProcessor):
    """Close the pipeline after the LLM response TTS completes."""

    async def process_frame(self, frame: Frame, direction: FrameDirection):
        await super().process_frame(frame, direction)
        await self.push_frame(frame, direction)
        if isinstance(frame, TTSStoppedFrame):
            logger.debug("TTS stopped — pushing EndFrame to close connection")
            await self.push_frame(EndFrame(), direction)


# ── Pipeline event logger ────────────────────────────────────────────────────


class PipelineLogger(FrameProcessor):
    """Logs key pipeline events: VAD, STT, LLM, TTS."""

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self._llm_tokens: list[str] = []

    async def process_frame(self, frame: Frame, direction: FrameDirection):
        await super().process_frame(frame, direction)

        if isinstance(frame, UserStartedSpeakingFrame):
            logger.info(">>> VAD: speech started")
        elif isinstance(frame, UserStoppedSpeakingFrame):
            logger.info(">>> VAD: speech ended")
        elif isinstance(frame, TranscriptionFrame):
            logger.info(">>> STT: %r", frame.text)
        elif isinstance(frame, LLMTextFrame):
            self._llm_tokens.append(frame.text)
        elif isinstance(frame, LLMFullResponseEndFrame):
            logger.info(">>> LLM: %r", "".join(self._llm_tokens))
            self._llm_tokens.clear()
        elif isinstance(frame, TTSStartedFrame):
            logger.info(">>> TTS: started")
        elif isinstance(frame, TTSStoppedFrame):
            logger.info(">>> TTS: stopped")

        await self.push_frame(frame, direction)


# ── FastAPI app ───────────────────────────────────────────────────────────────

app = FastAPI(title="EVA Bot", version="3.0")


@app.get("/health")
def health():
    return {"status": "ok", "model": MODEL, "piper": PIPER_URL}


@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    logger.info("Client connected from %s", websocket.client)

    async with aiohttp.ClientSession() as http_session:
        transport = FastAPIWebsocketTransport(
            websocket=websocket,
            params=FastAPIWebsocketParams(
                audio_in_enabled=True,
                audio_out_enabled=True,
                add_wav_header=False,
                serializer=RawPCMSerializer(),
            ),
        )

        stt = DeepgramSTTService(
            api_key=DEEPGRAM_API_KEY,
            sample_rate=MIC_RATE,
        )

        llm = OpenAILLMService(
            api_key=OPENROUTER_API_KEY,
            base_url="https://openrouter.ai/api/v1",
            model=MODEL,
        )

        tts = PiperHttpTTSService(
            base_url=PIPER_URL,
            aiohttp_session=http_session,
            sample_rate=TTS_RATE,
        )

        context = LLMContext([{"role": "system", "content": EVE_SYSTEM}])

        context_pair = LLMContextAggregatorPair(
            context,
            user_params=LLMUserAggregatorParams(
                vad_analyzer=SileroVADAnalyzer(
                    params=VADParams(
                        confidence=0.7,
                        start_secs=0.2,
                        stop_secs=0.8,
                    )
                )
            ),
        )

        pipeline = Pipeline(
            [
                transport.input(),
                PipelineLogger(),
                stt,
                context_pair.user(),
                llm,
                tts,
                TTSCompletionProcessor(),
                transport.output(),
                context_pair.assistant(),
            ]
        )

        task = PipelineTask(
            pipeline,
            params=PipelineParams(
                enable_metrics=True,
                enable_usage_metrics=True,
            ),
        )

        @transport.event_handler("on_client_connected")
        async def on_client_connected(transport, client):
            logger.info("on_client_connected — ready")

        @transport.event_handler("on_client_disconnected")
        async def on_client_disconnected(transport, client):
            logger.info("Client disconnected — cancelling task")
            await task.cancel()

        runner = PipelineRunner(handle_sigint=False)
        await runner.run(task)

    logger.info("WebSocket session closed")


# ── Entry point ───────────────────────────────────────────────────────────────

if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8765, log_level="info")
