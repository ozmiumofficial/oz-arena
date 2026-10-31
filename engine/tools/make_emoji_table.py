"""Write engine/src/oz/ui/oz_emoji_table.h: the chat's emoji codes (oz_emoji.c).

python engine/tools/make_emoji_table.py

Chat crosses the network as ASCII: every Quake 3 engine turns a byte above 127 in a network string
into a dot (msg.c MSG_ReadString), Quake3e and ioquake3 included. So an emoji goes as a code
(":fire:"), which any other client shows as it is, and Oz draws as the emoji from Windows' own
Segoe UI Emoji (a fallback font face, oz_ui.cpp).

The emoji are the font's color glyphs on single code points: the COLR table's layered glyphs, which
FreeType draws. No sequences (flags, skin tones, joined families): RmlUi lays text out a code point
at a time. Code points Barlow has (the HUD's and menus' font) draw as Barlow's letters, so they're
left out. Each emoji gets its Unicode name in lower case as a code (from Windows' ICU, newer than
Python's unicodedata), and the common ones short names as Discord and Slack spell them (ALIASES). The
first short name, else the Unicode name, is the code a message sends.

Needs fontTools, and Barlow fetched (build_engine.py). The output is committed: run this again for a
newer font or more short names.
"""
import ctypes, glob, os, re, sys

from fontTools.ttLib import TTFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(ROOT, "engine", "src", "oz", "ui", "oz_emoji_table.h")
FONT = os.path.join(os.environ.get("WINDIR", "C:/Windows"), "Fonts", "seguiemj.ttf")
BARLOW = os.path.join(ROOT, "build", "engine", "thirdparty", "barlow", "ofl", "barlow")

# not emoji alone: the flags' letters, skin tones, hair (parts of sequences)
SKIP = [(0x1F1E6, 0x1F1FF), (0x1F3FB, 0x1F3FF), (0x1F9B0, 0x1F9B3)]

# short names, each line "name [name ...] emoji"; the first name is the one sent
ALIASES = """
grinning 😀
smiley 😃
smile 😄
grin 😁
laughing satisfied 😆
sweat_smile 😅
rofl 🤣
joy 😂
slight_smile slightly_smiling_face 🙂
upside_down 🙃
melting 🫠
wink 😉
blush 😊
innocent halo 😇
smiling_hearts 🥰
heart_eyes 😍
star_struck 🤩
kissing_heart 😘
kissing 😗
relaxed ☺
kissing_closed_eyes 😚
smiling_tear 🥲
yum 😋
stuck_out_tongue tongue_out 😛
stuck_out_tongue_winking_eye 😜
zany crazy 🤪
stuck_out_tongue_closed_eyes 😝
money_mouth 🤑
hugs hugging 🤗
hand_over_mouth 🤭
shush shushing 🤫
thinking hmm 🤔
salute saluting 🫡
zipper_mouth 🤐
raised_eyebrow sus 🤨
neutral neutral_face 😐
expressionless 😑
no_mouth 😶
smirk 😏
unamused 😒
roll_eyes eyeroll 🙄
grimacing 😬
lying liar 🤥
relieved 😌
pensive 😔
sleepy 😪
drooling 🤤
sleeping 😴
mask 😷
sick 🤒
hurt head_bandage 🤕
nauseated 🤢
vomiting puke 🤮
sneezing 🤧
hot hot_face 🥵
cold cold_face 🥶
woozy 🥴
dizzy_face 😵
exploding_head mind_blown 🤯
cowboy 🤠
partying party 🥳
disguised 🥸
sunglasses cool 😎
nerd 🤓
monocle 🧐
confused 😕
worried 😟
slight_frown 🙁
frowning2 ☹
open_mouth 😮
hushed 😯
astonished 😲
flushed 😳
pleading 🥺
frowning 😦
anguished 😧
fearful 😨
cold_sweat 😰
disappointed_relieved 😥
cry 😢
sob 😭
scream 😱
confounded 😖
persevere 😣
disappointed 😞
sweat 😓
weary 😩
tired 😫
yawn yawning 🥱
triumph 😤
rage pout 😡
angry 😠
cursing 🤬
smiling_imp devil 😈
imp 👿
skull dead 💀
skull_crossbones ☠
poop 💩
clown 🤡
ogre 👹
goblin 👺
ghost 👻
alien 👽
space_invader 👾
robot 🤖
joy_cat 😹
heart_eyes_cat 😻
see_no_evil 🙈
hear_no_evil 🙉
speak_no_evil 🙊
heart ❤
orange_heart 🧡
yellow_heart 💛
green_heart 💚
blue_heart 💙
purple_heart 💜
black_heart 🖤
white_heart 🤍
brown_heart 🤎
broken_heart 💔
two_hearts 💕
sparkling_heart 💖
heartpulse 💗
heartbeat 💓
cupid 💘
heart_exclamation ❣
kiss 💋
100 💯
anger 💢
boom collision 💥
dizzy 💫
sweat_drops 💦
dash 💨
hole 🕳
bomb 💣
speech 💬
zzz 💤
wave 👋
raised_back_of_hand 🤚
hand raised_hand ✋
vulcan 🖖
ok_hand 👌
pinched 🤌
pinch 🤏
v peace ✌
crossed_fingers 🤞
love_you 🤟
metal 🤘
call_me 🤙
point_left 👈
point_right 👉
point_up_2 👆
point_down 👇
point_up ☝
middle_finger 🖕
thumbsup +1 👍
thumbsdown -1 👎
fist ✊
punch 👊
left_fist 🤛
right_fist 🤜
clap 👏
raised_hands 🙌
heart_hands 🫶
open_hands 👐
handshake 🤝
pray 🙏
writing_hand ✍
muscle flex 💪
eyes 👀
eye 👁
brain 🧠
tongue 👅
lips 👄
ear 👂
tooth 🦷
bone 🦴
shrug 🤷
facepalm 🤦
bow 🙇
no_good 🙅
ok_person 🙆
raising_hand 🙋
ninja 🥷
zombie 🧟
vampire 🧛
mage wizard 🧙
angel 👼
runner running 🏃
dancer 💃
detective 🕵
baby 👶
santa 🎅
fire lit 🔥
trophy 🏆
first_place gold_medal 🥇
second_place silver_medal 🥈
third_place bronze_medal 🥉
medal 🏅
crown 👑
gem 💎
dart bullseye 🎯
video_game 🎮
joystick 🕹
gun 🔫
dagger 🗡
knife 🔪
crossed_swords ⚔
shield 🛡
bow_and_arrow 🏹
axe 🪓
rocket 🚀
zap lightning ⚡
star ⭐
star2 glowing_star 🌟
sparkles ✨
tada 🎉
confetti 🎊
gift 🎁
balloon 🎈
fireworks 🎆
firecracker 🧨
moneybag 💰
money_with_wings 💸
dollar 💵
hourglass ⌛
stopwatch ⏱
alarm_clock ⏰
bell 🔔
mega 📣
loudspeaker 📢
lock 🔒
key 🔑
hammer 🔨
wrench 🔧
gear ⚙
warning ⚠
no_entry 🚫
stop 🛑
rotating_light siren 🚨
construction 🚧
x ❌
white_check_mark check ✅
heavy_check_mark ✔
question ❓
exclamation ❗
bangbang ‼
interrobang ⁉
red_circle 🔴
blue_circle 🔵
triangular_flag red_flag 🚩
checkered_flag 🏁
white_flag 🏳
crystal_ball 🔮
game_die dice 🎲
joker 🃏
slot_machine 🎰
8ball 🎱
chess_pawn ♟
soccer ⚽
basketball 🏀
boxing_glove 🥊
goat 🐐
chicken 🐔
snake 🐍
rat 🐀
shark 🦈
crab 🦀
sloth 🦥
turtle 🐢
snail 🐌
dog 🐶
cat 🐱
monkey 🐒
unicorn 🦄
spider 🕷
spider_web 🕸
bat 🦇
rose 🌹
salt salty 🧂
popcorn 🍿
pizza 🍕
beer 🍺
beers cheers 🍻
coffee ☕
cake 🎂
cookie 🍪
cheese 🧀
potato 🥔
candy 🍬
lollipop 🍭
jack_o_lantern pumpkin 🎃
ice ice_cube 🧊
sun sunny ☀
cloud ☁
snowflake ❄
rainbow 🌈
earth globe 🌍
moon crescent_moon 🌙
volcano 🌋
coffin ⚰
headstone 🪦
moai 🗿
ok 🆗
cool_button 🆒
new 🆕
free 🆓
sos 🆘
up 🆙
vs 🆚
arrow_up ⬆
arrow_down ⬇
arrow_left ⬅
arrow_right ➡
repeat 🔁
recycle ♻
infinity ♾
musical_note 🎵
notes music 🎶
headphones 🎧
microphone mic 🎤
violin 🎻
guitar 🎸
camera 📷
tv 📺
computer laptop 💻
keyboard ⌨
mouse_three_button computer_mouse 🖱
phone iphone 📱
battery 🔋
bulb idea 💡
mag 🔍
chart_up chart_with_upwards_trend 📈
chart_down chart_with_downwards_trend 📉
pushpin 📌
memo 📝
link 🔗
pill 💊
syringe 💉
bandage adhesive_bandage 🩹
drop_of_blood blood 🩸
radioactive ☢
biohazard ☣
ambulance 🚑
car 🚗
airplane ✈
ufo 🛸
anchor ⚓
snowman ⛄
christmas_tree 🎄
"""


def ucd_name(cp, icu=ctypes.CDLL("icu.dll")):
    """The code point's Unicode name from Windows' ICU (u_charName, U_UNICODE_CHAR_NAME)."""
    f = icu.u_charName
    f.restype = ctypes.c_int32
    f.argtypes = [ctypes.c_int32, ctypes.c_int, ctypes.c_char_p, ctypes.c_int32, ctypes.POINTER(ctypes.c_int)]
    buf, err = ctypes.create_string_buffer(256), ctypes.c_int(0)
    f(cp, 0, buf, 256, ctypes.byref(err))
    return buf.value.decode("ascii") if err.value <= 0 else ""


def code(name):
    return re.sub(r"_+", "_", re.sub(r"[^a-z0-9]", "_", name.lower())).strip("_")


def icu_version():
    v = (ctypes.c_uint8 * 4)()
    ctypes.CDLL("icu.dll").u_getUnicodeVersion(v)
    return "%d.%d" % (v[0], v[1])


def main():
    font = TTFont(FONT, lazy=True)
    cmap = font.getBestCmap()
    colr = font["COLR"].table
    layered = set(r.BaseGlyph for r in colr.BaseGlyphRecordArray.BaseGlyphRecord)
    barlow = set()
    for p in glob.glob(os.path.join(BARLOW, "*.ttf")):
        barlow |= set(TTFont(p, lazy=True).getBestCmap())
    if not barlow:
        sys.exit("Barlow isn't fetched (%s): run engine/build_engine.py first" % BARLOW)
    emoji = sorted(cp for cp, g in cmap.items() if g in layered and cp >= 0x80 and cp not in barlow
                   and not any(a <= cp <= b for a, b in SKIP))

    names, send, short_names = {}, {}, set()
    missing = []
    for line in ALIASES.strip().splitlines():
        *short, e = line.split()
        cp = ord(e[0])
        if cp not in emoji:
            missing.append(line)
            continue
        for n in short:
            if n in names and names[n] != cp:
                sys.exit("%s names two emoji" % n)
            names[n] = cp
            short_names.add(n)
        send.setdefault(cp, short[0])
    if missing:
        sys.exit("not among the font's emoji:\n" + "\n".join(missing))
    unnamed = 0
    for cp in emoji:
        n = code(ucd_name(cp))
        if not n:
            unnamed += 1
            continue
        names.setdefault(n, cp)
        send.setdefault(cp, n)
    for cp in emoji:
        send.setdefault(cp, "u%x" % cp)	# no name at all: its number
        names.setdefault(send[cp], cp)

    version = font["name"].getDebugName(5) or "?"
    out = [
        "/*",
        "Oz: the chat's emoji codes (oz_emoji.c), written by engine/tools/make_emoji_table.py from Segoe UI",
        "Emoji (%s) and Unicode %s's names: don't edit." % (version, icu_version()),
        "*/",
        "",
        "// every code, in strcmp order; 1 marks a short name",
        "static const ozEmojiName_t ozEmojiNames[] = {",
    ]
    for n in sorted(names, key=lambda s: s.encode()):
        out.append('\t{ "%s", 0x%x, %d },' % (n, names[n], n in short_names))
    out += ["};", "", "// each emoji's code to send, by code point", "static const ozEmojiSend_t ozEmojiSend[] = {"]
    for cp in emoji:
        out.append('\t{ 0x%x, "%s" },' % (cp, send[cp]))
    out += ["};", ""]
    with open(OUT, "w", newline="\n") as f:
        f.write("\n".join(out))
    print("%d emoji, %d codes (%d short names, %d without a Unicode name) -> %s"
          % (len(emoji), len(names), len(ALIASES.strip().splitlines()), unnamed, os.path.relpath(OUT, ROOT)))


if __name__ == "__main__":
    main()
