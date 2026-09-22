"""Generate app.ico for FloatKeys: dark rounded square + mini key grid + blue spacebar."""
from PIL import Image, ImageDraw

S = 256
img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)

# background: dark rounded square
d.rounded_rectangle([4, 4, S - 4, S - 4], radius=52, fill=(30, 30, 30, 255))

# rows of keys
key_fill = (69, 69, 69, 255)
blue = (0, 120, 212, 255)
x0, x1 = 34, S - 34
# row 1: 5 small keys
y = 52
kw = (x1 - x0 - 4 * 10) / 5
for i in range(5):
    x = x0 + i * (kw + 10)
    d.rounded_rectangle([x, y, x + kw, y + 40], radius=9, fill=key_fill)
# row 2: 5 small keys
y = 104
for i in range(5):
    x = x0 + i * (kw + 10)
    d.rounded_rectangle([x, y, x + kw, y + 40], radius=9, fill=key_fill)
# row 3: mod + blue spacebar + mod
y = 156
d.rounded_rectangle([x0, y, x0 + 44, y + 44], radius=9, fill=key_fill)
d.rounded_rectangle([x0 + 54, y, x1 - 54, y + 44], radius=9, fill=blue)
d.rounded_rectangle([x1 - 44, y, x1, y + 44], radius=9, fill=key_fill)

img.save("app.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48),
                           (64, 64), (128, 128), (256, 256)])
print("wrote app.ico")
