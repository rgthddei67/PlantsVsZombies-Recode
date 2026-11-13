"""Verify complete-character alpha against opaque references, not individual-part opacity."""
import json
import sys
from pathlib import Path
import struct
import zlib


def read_png(path):
    """Read the noninterlaced 8-bit RGB/RGBA PNGs emitted by AutoTest (stdlib only)."""
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    offset, compressed = 8, bytearray()
    while offset < len(data):
        size = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset+4:offset+8]
        payload = data[offset+8:offset+8+size]
        if kind == b"IHDR":
            width,height,depth,color,compression,filtering,interlace = struct.unpack(">IIBBBBB",payload)
            assert depth == 8 and color in (2,6) and interlace == 0
        elif kind == b"IDAT": compressed.extend(payload)
        offset += size+12
    channels = 4 if color == 6 else 3
    stride = width*channels
    raw = zlib.decompress(compressed)
    assert len(raw) == (stride+1)*height
    pixels = bytearray(stride*height)
    for y in range(height):
        kind = raw[y*(stride+1)]
        row = bytearray(raw[y*(stride+1)+1:(y+1)*(stride+1)])
        prior = pixels[(y-1)*stride:y*stride] if y else bytes(stride)
        assert kind in range(5)
        if kind:
            for i in range(stride):
                left = row[i-channels] if i >= channels else 0
                up = prior[i]
                corner = prior[i-channels] if i >= channels else 0
                if kind == 1: predictor = left
                elif kind == 2: predictor = up
                elif kind == 3: predictor = (left+up)//2
                else:
                    value = left+up-corner
                    a,b,c = abs(value-left),abs(value-up),abs(value-corner)
                    predictor = left if a <= b and a <= c else up if b <= c else corner
                row[i] = (row[i]+predictor)&255
        pixels[y*stride:(y+1)*stride] = row
    def pixel(x,y):
        start = (y*width+x)*channels
        return pixels[start:start+3]
    return width,height,pixel


def verify(folder):
    state = json.loads((folder / "status.json").read_text(encoding="utf-8"))
    assert state["status"] == "passed" and state["exitCode"] == 0, state
    before = json.loads((folder / "00_sound_baseline.json").read_text(encoding="utf-8"))
    after = json.loads((folder / "02_sound_clicks.json").read_text(encoding="utf-8"))
    assert after["cardClickSoundRequests"] == before["cardClickSoundRequests"] + 1
    assert after["cardFailedSoundRequests"] == before["cardFailedSoundRequests"] + 1
    for name in ["04_composite.png", "06_resized.png"]:
        width,height,pixel = read_png(folder/name)
        assert (width,height) == (1100,600), (width,height)
        background = (40,70,110)
        for offset in [320,640]:
            total = maximum = visible = 0
            count = 0
            for y in range(80,560):
                for x in range(100,350):
                    reference = pixel(x,y)
                    actual = pixel(x+offset,y)
                    visible += sum(abs(a-b) for a,b in zip(reference,background))
                    for a,b,bg in zip(actual,reference,background):
                        error = abs(a-(b+bg)//2)
                        total += error; maximum = max(maximum,error); count += 1
            mean = total/count
            assert visible/count > 2, "empty actor reference"
            assert mean < 0.8 and maximum <= 4, (folder.name,name,offset,mean,maximum)
            print(folder.name,name,offset,"mean",round(mean,4),"max",maximum)
    # Fullscreen letterbox can change sampling phase; verify flat interior regions independently.
    width,height,pixel = read_png(folder/"05_fullscreen.png")
    scale = min(width/1100,height/600)
    ox,oy = (width-1100*scale)/2,(height-600*scale)/2
    for column in [440,760]:
        for x,y,rgb in [(column+10,500,(125,60,70)),(column+45,520,(35,140,90))]:
            actual = pixel(round(ox+x*scale),round(oy+y*scale))
            assert max(abs(a-b) for a,b in zip(actual,rgb)) <= 3, (folder.name,"fullscreen",actual,rgb)
    print(folder.name, "audio and fullscreen passed")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit("usage: verify_preview_opacity.py <AutoTest output folder> [...] ")
    for arg in sys.argv[1:]:
        verify(Path(arg))
