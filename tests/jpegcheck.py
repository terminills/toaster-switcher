#!/usr/bin/env python3
"""jpegcheck.py DIR: decode jpegtest's pictures with Pillow (libjpeg) and measure, block of 16 by 16 for block, how far
each is from the picture as it was; libjpeg's own JPEG of the same picture at the encoder's settings (quality 80,
colour at half size both ways) is the measure of how far that may be. The two use the same tables and differ only in
how they round, so a block of ours is about as far off as libjpeg's; one that went wrong in the stream is far off."""
import glob
import io
import sys

from PIL import Image, ImageChops

LIMIT = 8       # of 255, a block's average


def blocks(a, b):
    """The average difference of each block, the three colours one after another."""
    return ImageChops.difference(a, b).reduce(16).tobytes()


worst, bad = 0, 0
names = sorted(glob.glob(sys.argv[1] + "/*.jpg"))
for name in names:
    src = Image.open(name[:-4] + ".ppm").convert("RGB")
    ref = io.BytesIO()
    src.save(ref, "JPEG", quality=80, subsampling=2)
    try:
        ours = Image.open(name)
        ours.load()
        if ours.size != src.size:
            raise ValueError("size %s, not %s" % (ours.size, src.size))
        over = max(a - b for a, b in zip(blocks(ours.convert("RGB"), src), blocks(Image.open(ref).convert("RGB"), src)))
    except Exception as e:
        print("%s: %s" % (name, e))
        bad += 1
        continue
    worst = max(worst, over)
    if over > LIMIT:
        print("%s: a block is %d of 255 further from the picture than libjpeg's" % (name, over))
        bad += 1
print("%d pictures, %d bad; no block more than %d of 255 further from the picture than libjpeg's" % (len(names), bad, worst)
      if not bad else "%d pictures, %d bad" % (len(names), bad))
sys.exit(1 if bad or not names else 0)
