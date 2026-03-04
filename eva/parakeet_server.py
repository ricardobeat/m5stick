"""
Minimal OpenAI-compatible STT server wrapping parakeet-mlx.
POST /v1/audio/transcriptions  (multipart, field: file)
Returns: {"text": "..."}
"""

import io
import tempfile
import os
from pathlib import Path

from fastapi import FastAPI, File, UploadFile
from fastapi.responses import JSONResponse
import uvicorn

app = FastAPI()
model = None

def get_model():
    global model
    if model is None:
        from parakeet_mlx import from_pretrained
        model = from_pretrained("mlx-community/parakeet-tdt-0.6b-v2")
    return model

@app.post("/v1/audio/transcriptions")
async def transcribe(file: UploadFile = File(...)):
    data = await file.read()
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        f.write(data)
        tmp = f.name
    try:
        result = get_model().transcribe(tmp)
        return JSONResponse({"text": result.text.strip()})
    finally:
        os.unlink(tmp)

@app.get("/health")
def health():
    return {"status": "ok"}

if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=5092)
