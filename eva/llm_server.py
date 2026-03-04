"""
LLM proxy server for EVA.
POST /chat  {"messages": [...]}  →  {"text": "..."}
Configure model and API key here; M5Stick never touches OpenRouter directly.
"""

import os
import httpx
from fastapi import FastAPI
from fastapi.responses import JSONResponse
from pydantic import BaseModel
import uvicorn

# ── Configure here ──────────────────────────────────────────
OPENROUTER_API_KEY = os.environ.get("OPENROUTER_API_KEY", "")
MODEL              = "qwen/qwen3.5-flash-02-23"  # change here to switch models
MAX_TOKENS         = 200
# ────────────────────────────────────────────────────────────

app = FastAPI()

class ChatRequest(BaseModel):
    messages: list[dict]

@app.post("/chat")
async def chat(req: ChatRequest):
    async with httpx.AsyncClient(timeout=20) as client:
        r = await client.post(
            "https://openrouter.ai/api/v1/chat/completions",
            headers={
                "Authorization": f"Bearer {OPENROUTER_API_KEY}",
                "HTTP-Referer": "https://eva.local",
                "X-Title": "EVA",
            },
            json={
                "model": MODEL,
                "max_tokens": MAX_TOKENS,
                "stream": False,
                "messages": req.messages,
            },
        )
    if r.status_code != 200:
        return JSONResponse({"error": r.text}, status_code=502)
    content = r.json()["choices"][0]["message"]["content"]
    return {"text": content}

@app.get("/health")
def health():
    return {"status": "ok", "model": MODEL}

if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=5093)
