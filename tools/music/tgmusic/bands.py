"""32 local bands. Each covers its stage's race tune (melody kept note for note) in a genre with personality.
Bass pattern: (beat offset, beats, degree R/5/3/7/8/A(approach)/W(walk), vel); comp: (offset, beats, vel, kind
C chord / S strum / U upper chop / L low root / A arpeggio / alb Alberti / T tremolo).

Only genre patterns, instrument choices and tempos: the melody each band plays is read from
the player's ROM at run time. Artists named in the stories are style references only."""
from .personality import G, S, L

BANDS = []


def band(country, city, tune, name, genre, bpm, story, swing=.5, swing16=.5, lift=0, reverb="rock", riser=True):
    def deco(fn):
        BANDS.append(dict(country=country, city=city, tune=tune, band=name, genre=genre, bpm=bpm, story=story,
                          swing=swing, swing16=swing16, lift=lift, reverb=reverb, riser=riser, fn=fn))
        return fn
    return deco


# ------------------------------------------------------------------ shared grooves / patterns
SWING = {0: {"p": "....x.......x..."},
         1: {"R": "x...x.x.x...x.x.", "p": "....x.......x..."},
         2: [{"R": "X...X.x.X...X.x.", "p": "....x.......x...", "k": "g...g...g...g...", "s": "......g.......g."},
             {"R": "X...X.x.X...X.x.", "p": "....x.......x...", "k": "g...g...g...g...", "s": "..g.......g...g."}],
         3: [{"R": "X...X.x.X...X.x.", "p": "....x.......x...", "k": "x...g...x.....x.", "s": "......x...g...X."},
             {"R": "X...X.x.X...X.x.", "p": "....x.......x...", "k": "x.....x.g...x...", "s": "..g...X.......x."}]}
FLOOR = {0: {}, 1: {"k": "X...X...X...X...", "h": "..x...x...x...x."},
         2: [{"k": "X...X...X...X...", "o": "..x...x...x...x.", "s": "....X.......X...", "h": "x.g.x.g.x.g.x.g."},
             {"k": "X...X...X...X...", "o": "..x...x...x...x.", "clap": "....X.......X...", "h": "xgxgxgxgxgxgxgxg"}],
         3: [{"k": "X...X...X...X...", "o": "..x...x...x...x.", "s": "....X.......X...", "clap": "....X.......X...",
              "h": "xgxgxgxgxgxgxgxg", "tamb": "..x...x...x...x."}]}
WALK = "walk"
OCT8 = [(o / 2, .45, "R" if o % 2 == 0 else "8", .95 if o % 2 == 0 else .75) for o in range(8)]
OFFBASS = [(o + .5, .4, "R", .9) for o in range(4)]
ROOT_FIFTH = [(0, 1, "R", .95), (2, 1, "5", .85)]
HOLD = [(0, 4, "R", .7)]
POMPE = [(0, .18, 1, "C"), (1, .28, .7, "C"), (2, .18, 1, "C"), (3, .28, .7, "C")]
OFFCHOP = [(.5, .2, .8, "U"), (1.5, .2, .8, "U"), (2.5, .2, .8, "U"), (3.5, .2, .8, "U")]
HOLDC = [(0, 3.9, .55, "C")]


# ================================================================== TUNE 1 (Las Vegas melody, C minor)
@band("USA", "Las Vegas", 1, "The Sands Room Orchestra", "Rat-Pack big-band swing", 172,
      "Sinatra-era showband on the Strip: swung ride, walking bass, sax soli then a trumpet shout chorus a semitone up, "
      "trombones answering with blues licks, jackpot-bell flourish", swing=.64, lift=1)
def las_vegas(B):
    B.drums(SWING, kit="jazz", fill="snare")
    B.bass(G(32), {0: HOLD, 1: ROOT_FIFTH, 2: WALK, 3: WALK}, low=31)
    B.comp("piano", G(0), {1: [(0, .4, .7, "C"), (2.5, .4, .65, "C")], 2: [[(1.5, .3, .7, "C"), (3, .3, .6, "C")],
           [(0, .3, .7, "C"), (2.5, .5, .65, "C")]], 3: [(1.5, .3, .7, "C"), (3.5, .3, .8, "C")], 0: [(0, 2, .5, "C")]},
           52, 70, 4, ext="jazz", gain=-11, pan=-0.25)
    B.comp("brass_shout", G(61), {3: [(0, .3, .95, "C"), (1.5, .4, .9, "C"), (3.5, .4, 1.0, "C")]},
           58, 77, 4, ext="jazz", gain=-10, pan=0.2, ant=True)
    B.pad("sax_pad", G(66), {2}, 50, 66, 3, ext="jazz", gain=-17)
    B.hook("vibes", G(11), {1, 2}, gain=-12, pan=0.3)
    B.lead([[L(G(65), -1, art="legato", orn=("grace", "slide"), name="alto_sax")],
            [L(G(56), -1, orn=("fall", "grace"), name="trumpet"), L(G(65), -9, oct=-12, art="legato", name="sax_under")]],
           brk=[L(G(11), -2, name="vibes_solo")],
           peak=[L(G(56), -1, orn=("fall",), name="trumpet_shout"), L(G(57), -6, oct=-12, name="bones_unison"),
                 L(G(65), -7, harm=-2, art="legato", name="sax_harmony")], scale="harmonic")
    B.answer("trombone", G(57), "blues", lo=48, hi=70, gain=-8, orn=("fall",))
    B.motif("jackpot", G(112), "vegas_bell", octave=5, gain=-9)


@band("South America", "Rio", 1, "Escola de Samba Unidos do Top Gear", "Samba-enredo batucada", 168,
      "Carnival parade: surdos, caixas, tamborims, agogô and cuíca; cavaquinho strums, 7-string bass runs, flute "
      "melody handed to a brass ala at the peak, apito whistle calls the breaks", swing16=.57, lift=0)
def rio(B):
    samba = {0: {"surm": "X.......X.......", "sur": "....X.......X...", "shk": "xgxgxgxgxgxgxgxg"},
             1: {"surm": "X.......X.......", "sur": "....X.......X...", "tamb": "X.xX.xX.X.xX.xX."},
             2: [{"surm": "X.......X.......", "sur": "....X.......X...", "s": "g.gxg.gXg.gxg.gX", "tamb": "X.xX.xX.X.xX.xX.",
                  "agoh": "X.x...x.X.x.x...", "agol": "..x.x.....x...x.", "shk": "xgxgxgxgxgxgxgxg"},
                 {"surm": "X.......X.......", "sur": "....X..x....X...", "s": "g.gxg.gXg.gxg.gX", "tamb": "X.X.xX.xX.X.xX.x",
                  "cuo": "..x...x...x.x...", "shk": "xgxgxgxgxgxgxgxg"}],
             3: [{"k": "X.......X.......", "surm": "X.......X.......", "sur": "....X..x....X..x", "s": "gxgxgxgXgxgxgxgX",
                  "tamb": "X.xX.xX.X.xX.xX.", "agoh": "X.x...x.X.x.x...", "agol": "..x.x.....x...x.", "cuo": "..x...x...x.x...",
                  "shk": "xgxgxgxgxgxgxgxg"}]}
    B.drums(samba, fill="latin", perc_gain=-6, gain=-6)
    sb = [(0, .75, "R", 1), (.75, .25, "R", .6), (1, .5, "5", .9), (1.5, .5, "R", .6), (2, .75, "R", 1),
          (2.75, .25, "R", .6), (3, .5, "5", .9), (3.5, .5, "A", .75)]
    B.bass(G(24), {0: HOLD, 1: ROOT_FIFTH, 2: sb, 3: sb}, low=36, gain=-4, name="violao_7")
    cav = [(0, .2, .9, "S"), (.5, .2, .6, "S"), (.75, .2, .8, "S"), (1.25, .2, .7, "S"), (1.75, .2, .8, "S"),
           (2, .2, .9, "S"), (2.5, .2, .6, "S"), (2.75, .2, .8, "S"), (3.25, .2, .7, "S"), (3.75, .2, .8, "S")]
    B.comp("cavaquinho", G(25), {2: cav, 3: cav, 1: [(0, .3, .8, "S"), (2, .3, .8, "S")]}, 62, 79, 4, gain=-9, pan=0.3)
    B.hook("marimba", G(12), {1, 2}, gain=-11, pan=-0.3)
    B.lead([[L(G(73), -1, orn=("grace",), name="flute")], [L(G(64), -1, art="legato", orn=("slide",), name="soprano_sax")]],
           brk=[L(G(24), -2, name="violao_solo")],
           peak=[L(G(61), -1, name="brass_ala"), L(G(73), -8, oct=12, name="flute_8va")], push=True, scale="dorian")
    B.answer("trombone", G(57), "dorian", lo=50, hi=72, gain=-9)
    B.motif("apito", G(0, 128), "whistle", perc=True, gain=-8)


@band("Japan", "Tokyo", 1, "Shibuya Lyricon Unit", "City-pop / J-fusion", 150,
      "80s Tokyo fusion (T-Square / Casiopea): slap bass, Rhodes 9th chords on the 16ths, Lyricon-style synth lead, "
      "brass stabs and a key change up for the last choruses", swing16=.54, lift=1)
def tokyo(B):
    g = {0: {"h": "x.x.x.x.x.x.x.x."}, 1: {"h": "x.x.x.x.x.x.x.x.", "rim": "....x.......x..."},
         2: [{"k": "X.....x...X.....", "s": "....X..g....X.g.", "h": "xgxgxgxgxgxgxgxg"},
             {"k": "X.....x.x.X.....", "s": "....X..g.g..X...", "h": "xgxgxgxgxgxgxgxo"}],
         3: [{"k": "X.....x...X..x..", "s": "....X..g....X.gg", "h": "xgxgxgxgxgxgxgxg", "tamb": "....x.......x..."}]}
    B.drums(g, kit="standard")
    slap = [(0, .25, "R", 1), (.5, .2, "8", .8), (.75, .2, "R", .5), (1.5, .25, "R", .8), (2, .25, "R", .9),
            (2.5, .2, "8", .85), (3, .25, "5", .7), (3.5, .2, "8", .8), (3.75, .2, "A", .6)]
    B.bass(G(36), {0: HOLD, 1: ROOT_FIFTH, 2: slap, 3: slap}, low=31)
    rh = [(0, .7, .7, "C"), (1.5, .4, .6, "C"), (2.75, .8, .7, "C")]
    B.comp("rhodes", G(4), {0: HOLDC, 1: rh, 2: rh, 3: rh}, 55, 74, 5, ext="soul", gain=-10, pan=-0.2, ant="even")
    B.comp("brass_stabs", G(61), {3: [(0, .25, 1, "U"), (1.75, .25, .9, "U"), (2.5, .5, .9, "U")]}, 62, 80, 4,
           ext="soul", gain=-11, pan=0.25)
    B.pad("strings", G(48), {3}, 60, 79, 4, gain=-16)
    B.hook("pluck", S("Plucks/The 1980s.fxp"), {1, 2}, gain=-11, pan=0.3, delay=.15)
    B.lead([[L(S("Leads/Sync Lead.fxp"), -1, orn=("slide",), art="legato", name="lyricon", delay=.12)],
            [L(G(65), -1, art="legato", orn=("grace",), name="alto_sax")]],
           brk=[L(G(4), -2, name="rhodes_solo")],
           peak=[L(S("Leads/Sync Lead.fxp"), -1, art="legato", name="lyricon_peak", delay=.12),
                 L(G(61), -7, oct=-12, name="brass_unison")], push=True)
    B.answer("guitar", G(27), "minpent", lo=55, hi=79, gain=-11, pan=0.35)


@band("Germany", "Munich", 1, "Musicland Studios", "Moroder Munich-disco", 128,
      "Giorgio Moroder's Munich sound: a 16th-note sequencer bass, four-on-the-floor, Moog lead and string machine; "
      "the break drops to the bare sequencer, then a Bavarian brass fanfare kicks the peak in", lift=0, reverb="synthwave")
def munich(B):
    B.drums(FLOOR, kit="electronic")
    seq = [(o / 4, .2, ["R", "R", "8", "R", "5", "R", "8", "R"][o % 8], .9 if o % 4 == 0 else .7) for o in range(16)]
    B.bass(S("Basses/Bass 2.fxp"), {0: seq, 1: seq, 2: seq, 3: seq}, low=33, gain=-3, name="sequencer")
    B.pad("string_machine", G(50), {2, 3}, 55, 76, 4, gain=-13)
    B.comp("brass", G(61), {3: [(0, .5, 1, "C"), (1.5, .25, .9, "U"), (2, .5, .9, "C")]}, 58, 77, 4, gain=-12)
    B.hook("arp", S("Plucks/The 1980s.fxp"), {1, 2, 3}, gain=-11, pan=-0.3, delay=.25)
    B.lead([[L(S("Leads/Moogy Saw.fxp"), -1, art="legato", name="moog", delay=.15)],
            [L(S("Leads/Moogy Saw.fxp"), -1, art="legato", name="moog2", delay=.15), L(G(54), -9, name="vocoder_voice")]],
           brk=[L(G(88), -3, art="legato", name="fantasia_pad")],
           peak=[L(S("Leads/Moogy Saw.fxp"), -1, art="legato", name="moog_peak", delay=.15),
                 L(S("Polysynths/Super Jupiter.fxp"), -9, oct=12, name="jupiter_8va")])
    B.motif("fanfare", G(61), "fanfare", octave=4, gain=-8, where=[B.peak_start])


@band("Scandinavia", "Stockholm", 1, "Polar Studios Pop Orchestra", "ABBA-style Europop", 132,
      "Dancing-Queen Stockholm: glissando piano, galloping octave bass, disco strings, choir 'aahs' on the peak, "
      "glockenspiel playing the original arp", lift=2)
def stockholm(B):
    g = {0: {}, 1: {"k": "X.......X.......", "h": "x.x.x.x.x.x.x.x."},
         2: [{"k": "X...X...X...X...", "s": "....X.......X...", "h": "x.x.x.x.x.x.x.x.", "tamb": "..x...x...x...x."}],
         3: [{"k": "X...X...X...X...", "s": "....X.......X...", "o": "..x...x...x...x.", "tamb": "xgxgxgxgxgxgxgxg",
              "clap": "....x.......x..."}]}
    B.drums(g)
    B.bass(G(33), {0: HOLD, 1: ROOT_FIFTH, 2: OCT8, 3: OCT8}, low=33)
    pn = [(o / 2, .4, .8 if o % 2 == 0 else .6, "C") for o in range(8)]
    B.comp("piano", G(0), {0: [(0, 3.8, .6, "A")], 1: [(0, 1.8, .7, "C"), (2, 1.8, .7, "C")], 2: pn, 3: pn},
           55, 76, 4, gain=-10, pan=-0.2)
    B.pad("disco_strings", G(48), {2, 3}, 62, 84, 4, gain=-14, pan=0.2)
    B.pad("choir", G(52), {3}, 60, 76, 4, gain=-15)
    B.hook("glock", G(9), {1, 2, 3}, gain=-13, pan=0.35)
    B.lead([[L(G(0), -1, name="piano_lead"), L(G(0), -8, oct=12, name="piano_8va")],
            [L(G(48), -2, art="legato", name="strings_lead")]],
           brk=[L(G(0), -2, name="piano_alone")],
           peak=[L(G(48), -1, art="legato", name="strings_peak"), L(G(52), -6, art="legato", name="choir_lead"),
                 L(G(0), -8, oct=12, name="piano_sparkle")])
    B.answer("strings_run", G(49), "aeolian", lo=67, hi=91, gain=-12,
             cells=[[(0, .25), (.25, .25), (.5, .25), (.75, .25), (1, 1)]])


@band("France", "Paris", 1, "Quintette du Hot Club de Top Gear", "Jazz manouche (Django)", 208,
      "Hot-club Paris: la pompe rhythm guitar, slapped double bass, Grappelli violin and Django guitar trading the "
      "melody, a musette accordion carrying the break", swing=.62, reverb="rock")
def paris(B):
    g = {1: {"p": "....x.......x..."}, 2: {"s": "g.g.X.g.g.g.X.g.", "p": "....x.......x..."},
         3: {"s": "g.g.X.g.g.g.X.g.", "p": "....x.......x...", "R": "X...X.x.X...X.x."}}
    B.drums(g, kit="brush", gain=-9, fill="snare")
    B.bass(G(32), {0: HOLD, 1: ROOT_FIFTH, 2: ROOT_FIFTH, 3: WALK}, low=31)
    B.comp("la_pompe", G(25), {1: POMPE, 2: POMPE, 3: POMPE}, 52, 67, 4, ext="jazz", gain=-8, pan=-0.3)
    B.pad("accordion_bed", G(21), {0}, 55, 70, 3, gain=-14)
    B.lead([[L(G(40), -1, art="legato", orn=("grace", "slide"), name="violin")],
            [L(G(26), -1, orn=("grace",), name="django_guitar")]],
           brk=[L(G(21), -1, art="legato", orn=("mordent",), name="musette")],
           peak=[L(G(40), -1, art="legato", orn=("grace",), name="violin_peak"), L(G(26), -6, oct=-12, name="guitar_octave")],
           scale="harmonic")
    B.answer("django_licks", G(26), "harmonic", lo=55, hi=81, gain=-9, pan=0.3, prob=.7,
             cells=[[(0, .33), (.33, .33), (.67, .33), (1, .33), (1.33, .33), (1.67, .33), (2, 1)],
                    [(0, .5), (.5, .5), (1, .5), (1.5, 1)]])


@band("Italy", "Pisa", 1, "Torre Pendente Disco Club", "Italo disco", 124,
      "Italo-disco from the leaning tower town: octave synth bass, vocoder choir, Hasselhoff arps, syncopated "
      "synth-brass stabs and the campanile's bells opening the night", reverb="synthwave", lift=0)
def pisa(B):
    B.drums(FLOOR, kit="electronic")
    B.bass(S("Basses/Bass 2.fxp"), {0: HOLD, 1: OCT8, 2: OCT8, 3: OCT8}, low=33, gain=-3)
    B.comp("synth_brass", S("Brass/OB-8 Jump.fxp"), {2: [(0, .25, .9, "C"), (.75, .25, .8, "C"), (2.5, .5, .85, "C")],
           3: [(0, .25, 1, "C"), (.75, .25, .9, "C"), (1.5, .25, .8, "C"), (2.5, .5, .9, "C")]}, 58, 77, 4, gain=-12)
    B.pad("synth_strings", G(50), {0, 1, 2, 3}, 55, 74, 4, gain=-15)
    B.hook("hasselhoff", S("Plucks/Hasselhoff.fxp"), {1, 2, 3}, gain=-11, pan=0.3, delay=.25)
    B.lead([[L(S("Leads/Square.fxp"), -1, name="square_lead", delay=.15), L(G(54), -9, name="vocoder")],
            [L(S("Leads/Moogy Saw.fxp"), -1, art="legato", name="saw_lead", delay=.15)]],
           brk=[L(G(54), -1, art="legato", name="vocoder_solo")],
           peak=[L(S("Leads/Square.fxp"), -1, name="square_peak", delay=.15), L(G(54), -7, oct=12, name="vocoder_8va"),
                 L(G(62), -9, oct=-12, name="synthbrass_under")])
    B.motif("campanile", G(14), "westminster", octave=4, gain=-9, where=[0])


@band("UK", "London", 1, "Big Ben Soundsystem", "Liquid drum & bass", 174,
      "London jungle / liquid DnB: Big Ben chimes, chopped breakbeats with ghost snares, a rolling reese-style bass, "
      "Rhodes and strings carrying the melody, a half-time break", swing16=.53, reverb="synthwave")
def london(B):
    g = {0: {"h": "x.x.x.x.x.x.x.x."}, 1: {"h": "x.x.x.x.x.x.x.x.", "rim": "....x.......x..."},
         2: [{"k": "X.........X.....", "s": "....X..g.g..X..g", "h": "x.x.x.x.x.x.x.x."},
             {"k": "X.x.......X..x..", "s": "....X..g.g..X.g.", "h": "x.x.x.x.x.x.x.xo"}],
         3: [{"k": "X.x.......X.....", "s": "....X..gxg..X.gg", "h": "xgxgxgxgxgxgxgxg", "shk": "xgxgxgxgxgxgxgxg"},
             {"k": "X.....x...X.x...", "s": "....X..g.g.gX..g", "h": "xgxgxgxgxgxgxgxo", "shk": "xgxgxgxgxgxgxgxg"}]}
    B.drums(g, kit="room", fill="snare")
    reese = [(0, 1.5, "R", 1), (1.5, .5, "R", .7), (2.5, 1.4, "R", .9)]
    B.bass(S("Basses/Wide Bassline.fxp"), {0: HOLD, 1: HOLD, 2: reese, 3: reese}, low=28, gain=-3, name="reese")
    rh = [(0, 1.5, .65, "C"), (2.5, 1.2, .6, "C")]
    B.comp("rhodes", G(4), {0: HOLDC, 1: rh, 2: rh, 3: rh}, 55, 74, 5, ext="soul", gain=-11, pan=-0.2)
    B.pad("strings", G(49), {2, 3}, 60, 81, 4, gain=-14, pan=0.2)
    B.hook("bells", G(14), {1}, gain=-12, pan=0.2)
    B.lead([[L(G(48), -1, art="legato", name="strings_lead")], [L(G(4), -1, name="rhodes_lead", delay=.2)]],
           brk=[L(G(4), -1, name="rhodes_alone", delay=.25)],
           peak=[L(S("Leads/Sync Lead.fxp"), -1, art="legato", name="lead_synth", delay=.15),
                 L(G(48), -7, art="legato", oct=-12, name="strings_under")])
    B.answer("synth", S("Leads/Square.fxp"), "minpent", lo=62, hi=86, gain=-12, delay=.2)
    B.motif("big_ben", G(14), "westminster", octave=4, gain=-7)


# ================================================================== TUNE 2 (Hiroshima melody, D minor)
@band("USA", "Los Angeles", 2, "Sunset Blvd Lowriders", "West-coast G-funk", 100,
      "Dr.-Dre-era LA: laid-back swung 16ths, whiny portamento synth lead, Rhodes 9ths, Moog bass with slides, "
      "claps on 2 and 4, talkbox-style voice answering", swing16=.6, reverb="synthwave")
def los_angeles(B):
    g = {0: {"h": "x.x.x.x.x.x.x.x."}, 1: {"k": "X.........x.....", "clap": "....X.......X...", "h": "x.x.x.x.x.x.x.x."},
         2: [{"k": "X.....x...x.....", "clap": "....X.......X...", "h": "xgxgxgxgxgxgxgxg"},
             {"k": "X.....x...x..x..", "clap": "....X.......X..g", "h": "xgxgxgxgxgxgxgxo"}],
         3: [{"k": "X.....x...x..x..", "clap": "....X.......X...", "s": "....x.......x...", "h": "xgxgxgxgxgxgxgxg",
              "shk": "..x...x...x...x."}]}
    B.drums(g, kit="808", fill="snare", gain=-5)
    gb = [(0, .75, "R", 1), (1.5, .25, "8", .7), (1.75, .5, "R", .8), (2.75, .25, "5", .7), (3, .75, "R", .9), (3.75, .25, "A", .6)]
    B.bass(S("Basses/Rubber Bass.fxp"), {0: HOLD, 1: gb, 2: gb, 3: gb}, low=28, gain=-3, name="moog_bass")
    B.comp("rhodes", G(4), {0: HOLDC, 1: [(0, 1.5, .6, "C"), (2.5, 1, .55, "C")], 2: [(0, 1.5, .6, "C"), (2.5, 1, .55, "C")],
           3: [(0, 1.5, .65, "C"), (2.5, 1, .6, "C")]}, 55, 74, 5, ext="soul", gain=-10, pan=-0.25)
    B.pad("strings", G(49), {3}, 60, 79, 4, gain=-17)
    B.hook("bell_pluck", G(11), {1, 2}, gain=-13, pan=0.3)
    B.lead([[L(S("Plucks/Pure Square.fxp"), -1, art="legato", orn=("slide",), name="whine_lead", delay=.12)],
            [L(S("Leads/Sync Lead.fxp"), -1, art="legato", orn=("slide",), name="sync_whine", delay=.12)]],
           brk=[L(G(4), -2, name="rhodes_solo")],
           peak=[L(S("Plucks/Pure Square.fxp"), -1, art="legato", name="whine_peak", delay=.12),
                 L(G(54), -8, oct=-12, name="talkbox_voice")], push=True, scale="dorian")
    B.answer("talkbox", G(54), "minpent", lo=55, hi=76, gain=-10, pan=0.3)


@band("South America", "Machu Picchu", 2, "Los Condores del Inti", "Andean huayno", 152,
      "Inca highlands: quena flute with grace notes, zampoña panpipes doubling, charango strumming 16ths, Andean harp "
      "playing the original arp, bombo legüero and cajón; the break is a lone quena over wind", swing16=.55, reverb="orchestral")
def machu_picchu(B):
    g = {0: {}, 1: {"t5": "X.....x.X.......", "shk": "x.x.x.x.x.x.x.x."},
         2: [{"t5": "X.....x.X...x...", "rim": "....x.......x..x", "cab": "xgxgxgxgxgxgxgxg"}],
         3: [{"t5": "X.....x.X..xX...", "t3": "......x.......x.", "rim": "....x..x....x..x", "cab": "xgxgxgxgxgxgxgxg"}]}
    B.drums(g, kit="standard", fill="toms", gain=-6, perc_gain=-9)
    hb = [(0, .75, "R", 1), (.75, .25, "R", .6), (1, 1, "5", .85), (2, .75, "R", 1), (2.75, .25, "5", .6), (3, 1, "R", .8)]
    B.bass(G(32), {0: HOLD, 1: ROOT_FIFTH, 2: hb, 3: hb}, low=33, gain=-6, name="bass_guitarron")
    ch = [(0, .2, .9, "S"), (.25, .2, .5, "S"), (.5, .2, .7, "S"), (.75, .2, .5, "S"), (1, .2, .8, "S"), (1.5, .2, .7, "S"),
          (2, .2, .9, "S"), (2.25, .2, .5, "S"), (2.5, .2, .7, "S"), (2.75, .2, .5, "S"), (3, .2, .8, "S"), (3.5, .2, .7, "S")]
    B.comp("charango", G(25), {1: [(0, .3, .8, "S"), (2, .3, .8, "S")], 2: ch, 3: ch}, 64, 81, 4, gain=-10, pan=0.3)
    B.hook("andean_harp", G(46), {1, 2, 3}, gain=-10, pan=-0.3)
    B.pad("wind", G(92), {0, 1}, 50, 69, 3, gain=-18)
    B.lead([[L(G(73), -1, orn=("grace", "mordent"), name="quena")],
            [L(G(75), -1, art="legato", orn=("grace",), name="zampona"), L(G(73), -9, oct=12, name="quena_8va")]],
           brk=[L(G(73), -1, art="legato", orn=("slide", "grace"), name="lone_quena")],
           peak=[L(G(73), -1, orn=("grace",), name="quena_peak"), L(G(75), -5, harm=-2, name="zampona_3rds"),
                 L(G(40), -9, oct=-12, art="legato", name="violin_under")], scale="aeolian")
    B.answer("zampona", G(75), "minpent", lo=62, hi=86, gain=-10, pan=0.3)
    B.motif("condor", G(77), "lur", octave=5, gain=-9, where=[0])


@band("Japan", "Hiroshima", 2, "Wadaiko Hibiki", "Wagakki rock band", 160,
      "Japanese instruments with a modern band (Wagakki Band style): shakuhachi and shinobue trading the melody, "
      "tsugaru shamisen 16ths, koto arpeggios, taiko ensemble; the Peace Bell tolls into the quiet break", lift=0,
      reverb="orchestral")
def hiroshima(B):
    g = {0: {"TKL": "X...............", }, 1: {"TK": "X.....x.X.......", "TKL": "X.......X......."},
         2: [{"k": "X.....x.X.......", "s": "....X.......X...", "h": "x.x.x.x.x.x.x.x.", "TK": "X..x..x.X..x..x."}],
         3: [{"k": "X.x...x.X.x.....", "s": "....X.......X..g", "h": "xgxgxgxgxgxgxgxg", "TK": "X.xX.xX.X.xX.xX.",
              "TKL": "X.......X......."}]}
    B.drums(g, kit="power", fill="taiko", gain=-5, roll="TK")
    B.bass(G(33), {0: HOLD, 1: ROOT_FIFTH, 2: OCT8, 3: OCT8}, low=33, gain=-5)
    sh = [(o / 4, .15, .9 if o % 4 == 0 else .6, "S") for o in range(16)]
    B.comp("tsugaru_shamisen", G(106), {2: sh, 3: sh, 1: [(0, .3, .9, "S"), (1.5, .3, .7, "S"), (3, .3, .8, "S")]},
           57, 74, 2, gain=-11, pan=-0.3)
    B.hook("koto", G(107), {1, 2, 3}, gain=-10, pan=0.3)
    B.pad("strings", G(48), {3}, 58, 77, 4, gain=-16)
    B.lead([[L(G(77), -1, art="legato", orn=("slide", "cut"), name="shakuhachi")],
            [L(G(72), -1, orn=("cut", "grace"), name="shinobue")]],
           brk=[L(G(77), -1, art="legato", orn=("slide",), name="shakuhachi_alone")],
           peak=[L(G(72), -1, orn=("cut",), name="shinobue_peak"), L(G(106), -6, oct=-12, name="shamisen_unison"),
                 L(G(77), -8, oct=-12, art="legato", name="shakuhachi_under")], scale="in")
    B.answer("koto_answer", G(107), "in", lo=60, hi=84, gain=-10)
    B.motif("peace_bell", G(14), "temple", octave=3, gain=-6, where=[0, B.stop_beat] if hasattr(B, "stop_beat") else None)


@band("Germany", "Cologne", 2, "Kölsche Jecken Kapelle", "Karneval brass march", 152,
      "Kölner Karneval: tuba oompah, offbeat brass, clarinets and trumpets on the tune, accordion sing-along, "
      "big bass drum and cymbals; the Dom bell opens and the crowd 'schunkelt' through the break", lift=1)
def cologne(B):
    g = {0: {}, 1: {"k": "X.......X.......", "s": "....x.......x..."},
         2: [{"k": "X.......X.......", "C": "....x.......x...", "s": "..x...x...x...x."}],
         3: [{"k": "X.......X.......", "C": "....X.......X...", "s": "..x.g.x...x.g.x.", "tamb": "..x...x...x...x."}]}
    B.drums(g, kit="orchestra", fill="snare", gain=-5)
    ump = [(0, .8, "R", 1), (2, .8, "5", .9)]
    B.bass(G(58), {0: HOLD, 1: ump, 2: ump, 3: ump}, low=29, gain=-4, name="tuba")
    B.comp("brass_offbeats", G(61), {1: [(1, .3, .6, "C"), (3, .3, .6, "C")], 2: [(1, .3, .7, "C"), (3, .3, .7, "C")],
           3: [(1, .3, .8, "C"), (3, .3, .8, "C"), (3.5, .2, .6, "C")]}, 55, 72, 3, gain=-10, pan=0.25)
    B.comp("accordion", G(21), {0: HOLDC, 2: [(1, .4, .6, "C"), (3, .4, .6, "C")]}, 57, 74, 3, gain=-13, pan=-0.3)
    B.hook("glock", G(9), {1, 2}, gain=-14, pan=0.3)
    B.lead([[L(G(71), -1, orn=("grace",), name="clarinet")],
            [L(G(56), -1, name="trumpet"), L(G(71), -8, oct=12, name="clarinet_8va")]],
           brk=[L(G(21), -1, art="legato", name="accordion_singalong")],
           peak=[L(G(56), -1, name="trumpets_peak"), L(G(56), -5, harm=-2, name="trumpet_2"),
                 L(G(57), -7, oct=-12, name="trombones")], scale="harmonic")
    B.answer("trombone", G(57), "harmonic", lo=48, hi=70, gain=-9, orn=("fall",))
    B.motif("dom_bell", G(14), "westminster", octave=4, gain=-8, where=[0])


@band("Scandinavia", "Copenhagen", 2, "Little Mermaid Eurodance", "90s Eurodance (Aqua-style)", 138,
      "Copenhagen bubblegum dance: music-box fairy-tale intro (Andersen), offbeat bass, piano-house stabs, "
      "squeaky synth lead, supersaw peak", reverb="synthwave", lift=2)
def copenhagen(B):
    B.drums(FLOOR, kit="electronic", gain=-4)
    B.bass(S("Basses/Bass 2.fxp"), {0: HOLD, 1: OFFBASS, 2: OFFBASS, 3: OFFBASS}, low=33, gain=-3)
    st = [(0, .3, .9, "C"), (.75, .3, .8, "C"), (1.5, .3, .8, "C"), (2.5, .3, .8, "C"), (3.25, .3, .7, "C")]
    B.comp("house_piano", G(0), {2: st, 3: st, 0: [(0, 3.8, .55, "A")]}, 60, 79, 4, gain=-10, pan=-0.2)
    B.pad("supersaw", S("Polysynths/Super Jupiter.fxp"), {3}, 58, 77, 4, gain=-15)
    B.hook("music_box", G(10), {0, 1, 2}, gain=-11, pan=0.3)
    B.lead([[L(S("Leads/Square.fxp"), -1, name="squeak_lead", delay=.15)],
            [L(S("Leads/Square.fxp"), -1, name="squeak2", delay=.15), L(G(10), -8, oct=12, name="musicbox_8va")]],
           brk=[L(G(10), -1, name="music_box_alone")],
           peak=[L(S("Polysynths/Super Jupiter.fxp"), -1, name="supersaw_lead", delay=.12),
                 L(S("Leads/Square.fxp"), -7, oct=12, name="square_8va")])
    B.answer("piano", G(0), "aeolian", lo=67, hi=88, gain=-11)


@band("France", "Nice", 2, "Promenade des Anglais Trio", "Riviera bossa nova", 128,
      "Côte d'Azur cocktail bossa: nylon guitar batida with even-bar anticipations, brushed rim clave, flute then tenor "
      "sax on the melody, vibraphone playing the original arp, strings on the peak", swing16=.53, reverb="faithful")
def nice(B):
    g = {0: {}, 1: {"rim": "x..x..x...x..x..", "shk": "xgxgxgxgxgxgxgxg"},
         2: {"k": "X..x....X..x....", "rim": "x..x..x...x..x..", "shk": "xgxgxgxgxgxgxgxg"},
         3: {"k": "X..x....X..x....", "rim": "x..x..x...x..x..", "h": "x.x.x.x.x.x.x.x.", "shk": "xgxgxgxgxgxgxgxg"}}
    B.drums(g, kit="brush", fill="none", gain=-8, crash=False)
    bb = [(0, 1.4, "R", 1), (1.5, .5, "5", .8), (2, 1.4, "5", .9), (3.5, .5, "R", .75)]
    B.bass(G(32), {0: HOLD, 1: bb, 2: bb, 3: bb}, low=31, gain=-5)
    bat = [(0, .4, .7, "C"), (.75, .3, .6, "C"), (1.5, .4, .65, "C"), (2.5, .3, .6, "C"), (3.25, .5, .65, "C")]
    B.comp("nylon_batida", G(24), {0: HOLDC, 1: bat, 2: bat, 3: bat}, 52, 70, 4, ext="soul", gain=-8, pan=-0.3, ant="even")
    B.pad("strings", G(49), {3}, 60, 79, 4, ext="soul", gain=-15, pan=0.2)
    B.hook("vibes", G(11), {1, 2}, gain=-12, pan=0.3)
    B.lead([[L(G(73), -1, art="legato", orn=("grace",), name="flute")],
            [L(G(66), -1, art="legato", orn=("slide",), name="tenor_sax")]],
           brk=[L(G(24), -1, name="guitar_solo")],
           peak=[L(G(66), -1, art="legato", name="sax_peak"), L(G(73), -6, oct=12, art="legato", name="flute_8va")],
           push=True, scale="dorian")
    B.answer("vibes_answer", G(11), "dorian", lo=62, hi=86, gain=-11, pan=0.3)


@band("Italy", "Rome", 2, "Orchestra Spaghetti Western", "Morricone western epic", 150,
      "Ennio Morricone in Cinecittà: whistled melody, twangy guitar, galloping drums, wordless choir, a lonely "
      "trumpet de guello in the break, harmonica answers and a clock ticking", reverb="orchestral", lift=0)
def rome(B):
    g = {0: {"wbh": "x...x...x...x...", "wbl": "..x...x...x...x."},
         1: {"t5": "X.xxX.xxX.xxX.xx", "wbh": "x...x...x...x..."},
         2: [{"t5": "X.xxX.xxX.xxX.xx", "s": "....x.......x...", "C": "x..............."}],
         3: [{"k": "X.xxX.xxX.xxX.xx", "t5": "X.xxX.xxX.xxX.xx", "s": "....X.......X...", "tamb": "xgxgxgxgxgxgxgxg"}]}
    B.drums(g, kit="orchestra", fill="toms", gain=-6, perc_gain=-9)
    gal = [(0, .5, "R", 1), (.75, .25, "R", .6), (1, .5, "5", .9), (2, .5, "R", 1), (2.75, .25, "R", .6), (3, .5, "5", .9)]
    B.bass(G(33), {0: HOLD, 1: gal, 2: gal, 3: gal}, low=31, gain=-5)
    tw = [(0, .3, .9, "L"), (1, .3, .7, "S"), (2, .3, .9, "L"), (3, .3, .7, "S")]
    B.comp("twang_guitar", G(27), {1: tw, 2: tw, 3: tw}, 52, 72, 3, gain=-10, pan=-0.3)
    B.pad("choir", G(52), {2, 3}, 57, 77, 4, gain=-12)
    B.pad("strings", G(48), {3}, 50, 74, 4, gain=-14)
    B.hook("harpsichord", G(6), {1, 2}, gain=-13, pan=0.3)
    B.lead([[L(G(78), -1, art="legato", orn=("slide",), name="whistle")],
            [L(G(27), -1, orn=("slide",), name="twang_lead"), L(G(78), -9, oct=12, name="whistle_8va")]],
           brk=[L(G(56), -1, art="legato", orn=("grace",), name="deguello_trumpet")],
           peak=[L(G(52), -2, art="legato", name="choir_lead"), L(G(56), -4, name="trumpet_peak"),
                 L(G(48), -7, oct=-12, art="legato", name="strings_under")], scale="harmonic")
    B.answer("harmonica", G(22), "minpent", lo=60, hi=84, gain=-10, pan=0.3)
    B.motif("bell", G(14), "temple", octave=4, gain=-9, where=[0])


@band("UK", "Sheffield", 2, "The Steel City Synthetics", "80s Sheffield synth-pop", 140,
      "Human League / Heaven 17 / ABC: drum machine, Rubber bass 8ths, OB-8 stabs, square-wave lead, "
      "Jupiter strings and a cold, bare break", reverb="synthwave", lift=0)
def sheffield(B):
    g = {0: {"h": "x.x.x.x.x.x.x.x."}, 1: {"k": "X.......X.......", "h": "x.x.x.x.x.x.x.x."},
         2: [{"k": "X.......X.x.....", "s": "....X.......X...", "h": "x.x.x.x.x.x.x.x."},
             {"k": "X.......X.x.....", "clap": "....X.......X...", "h": "x.x.x.x.x.x.x.xo"}],
         3: [{"k": "X...X...X...X...", "s": "....X.......X...", "clap": "....X.......X...", "h": "xgxgxgxgxgxgxgxg",
              "tamb": "....x.......x..."}]}
    B.drums(g, kit="electronic", fill="toms")
    e8 = [(o / 2, .4, "R", .95 if o % 2 == 0 else .75) for o in range(8)]
    B.bass(S("Basses/Rubber Bass.fxp"), {0: HOLD, 1: e8, 2: e8, 3: OCT8}, low=33, gain=-3)
    B.comp("ob8_stabs", S("Brass/OB-8 Jump.fxp"), {2: [(0, .25, .9, "C"), (1.5, .25, .8, "C"), (3, .5, .85, "C")],
           3: [(0, .25, 1, "C"), (.75, .25, .8, "C"), (1.5, .25, .9, "C"), (3, .5, .9, "C")]}, 58, 77, 4, gain=-12, pan=0.2)
    B.pad("jupiter", S("Polysynths/Super Jupiter.fxp"), {1, 3}, 55, 74, 4, gain=-15)
    B.hook("square_pop", S("Plucks/Square Pop.fxp"), {1, 2}, gain=-12, pan=-0.3, delay=.2)
    B.lead([[L(S("Leads/Square.fxp"), -1, name="square_lead", delay=.15)],
            [L(S("Leads/Moogy Saw.fxp"), -1, art="legato", name="saw_lead", delay=.15)]],
           brk=[L(S("Pads/MKS-70 Warm Pad.fxp"), -2, art="legato", name="cold_pad_lead")],
           peak=[L(S("Leads/Square.fxp"), -1, name="square_peak", delay=.15),
                 L(S("Polysynths/Super Jupiter.fxp"), -7, oct=12, name="jupiter_8va")])
    B.answer("pop", S("Plucks/Square Pop.fxp"), "aeolian", lo=62, hi=86, gain=-12, delay=.2)


# ================================================================== TUNE 3 (Bordeaux melody, C minor, riff-driven)
@band("USA", "New York", 3, "The 42nd Street Precinct", "70s cop-show funk", 118,
      "Shaft / Starsky & Hutch Manhattan: wah-scratch guitar and clavinet 16ths, finger-bass funk, congas, "
      "brass section on the tune, a flute answering in the gaps, strings sweeping into the chase", swing16=.54, lift=1)
def new_york(B):
    g = {0: {"h": "xgxgxgxgxgxgxgxg"}, 1: {"k": "X.........X.....", "h": "xgxgxgxgxgxgxgxg", "rim": "....x.......x..."},
         2: [{"k": "X..x......X..x..", "s": "....X..g.g..X...", "h": "xgxgxgxgxgxgxgxo", "conga": "..x...x...x.x..."},
             {"k": "X.........X.x...", "s": "....X..g....X..g", "h": "xgxgxgxgxgxgxgxg", "lconga": "x...x..x..x....."}],
         3: [{"k": "X..x......X..x..", "s": "....X..g.g..X..g", "o": "..............x.", "h": "xgxgxgxgxgxgxgx.",
              "conga": "..x...x...x.x...", "lconga": "x...x..x..x.....", "tamb": "....x.......x..."}]}
    B.drums(g, kit="standard", fill="snare", perc_gain=-9)
    fk = [(0, .5, "R", 1), (.75, .25, "8", .7), (1.25, .25, "R", .6), (1.5, .4, "5", .8), (2.5, .25, "R", .9),
          (2.75, .25, "7", .7), (3, .5, "8", .8), (3.75, .25, "A", .6)]
    B.bass(G(33), {0: HOLD, 1: ROOT_FIFTH, 2: fk, 3: fk}, low=31, gain=-3, name="funk_bass")
    sc = [(o / 4, .12, .8 if o % 4 == 2 else .45, "U") for o in range(16)]
    B.comp("scratch_guitar", G(28), {1: sc, 2: sc, 3: sc}, 60, 76, 3, ext="soul", gain=-12, pan=-0.35)
    B.comp("clavinet", G(7), {2: [(0, .2, .9, "C"), (.75, .2, .7, "C"), (1.5, .2, .8, "C"), (2.75, .2, .7, "C"), (3.25, .2, .8, "C")],
           3: [(0, .2, .9, "C"), (.75, .2, .7, "C"), (1.5, .2, .8, "C"), (2.75, .2, .7, "C"), (3.25, .2, .8, "C")]},
           55, 72, 3, ext="soul", gain=-12, pan=0.3)
    B.pad("strings", G(48), {3}, 60, 82, 4, ext="soul", gain=-14)
    B.hook("rhodes", G(4), {0, 1}, gain=-12, pan=0.2)
    B.lead([[L(G(61), -1, name="brass_section"), L(G(66), -6, oct=-12, name="tenor_under")],
            [L(G(66), -1, art="legato", orn=("grace", "fall"), name="tenor_sax")]],
           brk=[L(G(73), -1, art="legato", orn=("grace",), name="flute_break")],
           peak=[L(G(56), -1, orn=("fall",), name="trumpet_lead"), L(G(61), -4, harm=-2, name="brass_3rds"),
                 L(G(57), -7, oct=-12, name="bones")], push=True, scale="dorian")
    B.answer("shaft_flute", G(73), "minpent", lo=67, hi=91, gain=-10, pan=0.3)


@band("South America", "Chichen Itza", 3, "Mariachi Kukulkán", "Mariachi ranchera", 180,
      "A mariachi at the pyramid: two trumpets in thirds, violins, vihuela strums, guitarrón walking roots and fifths, "
      "marimba playing the riff, a conch shell sounding the start, grito-style trumpet fall in the peak", swing=.5, lift=1,
      reverb="faithful")
def chichen_itza(B):
    g = {0: {}, 1: {"shk": "x.x.x.x.x.x.x.x."}, 2: {"k": "X.......X.......", "shk": "x.x.x.x.x.x.x.x.", "cab": "....x.......x..."},
         3: {"k": "X.......X.......", "s": "....x.......x...", "shk": "xgxgxgxgxgxgxgxg", "tamb": "....x.......x..."}}
    B.drums(g, kit="standard", fill="latin", gain=-9, perc_gain=-10, crash=False)
    B.bass(G(32), {0: HOLD, 1: ROOT_FIFTH, 2: ROOT_FIFTH, 3: [(0, .8, "R", 1), (1, .8, "5", .8), (2, .8, "R", .95), (3, .8, "A", .8)]},
           low=31, gain=-4, name="guitarron")
    vih = [(0, .2, .6, "L"), (1, .25, .9, "S"), (1.5, .2, .6, "S"), (2, .2, .6, "L"), (3, .25, .9, "S"), (3.5, .2, .6, "S")]
    B.comp("vihuela", G(25), {1: vih, 2: vih, 3: vih}, 55, 74, 4, gain=-9, pan=-0.3)
    B.pad("violins", G(48), {2, 3}, 62, 84, 3, gain=-14, pan=0.25)
    B.hook("marimba", G(12), {1, 2, 3}, gain=-10, pan=0.3)
    B.lead([[L(G(56), -1, orn=("grace",), name="trumpet"), L(G(56), -5, harm=-2, name="trumpet_3rds")],
            [L(G(40), -1, art="legato", orn=("grace", "slide"), name="violins"), L(G(40), -6, oct=-12, name="violins_low")]],
           brk=[L(G(24), -1, name="requinto_guitar")],
           peak=[L(G(56), -1, orn=("fall", "grace"), name="trumpet_peak"), L(G(56), -4, harm=-2, name="trumpet2_peak"),
                 L(G(40), -6, oct=12, art="legato", name="violins_8va")], scale="harmonic")
    B.answer("violin_answer", G(40), "harmonic", lo=62, hi=86, gain=-10, pan=0.3)
    B.motif("conch", G(60), "lur", octave=3, gain=-7, where=[0])


@band("Japan", "Yokohama", 3, "Chinatown Lion Dance Troupe", "Chinatown kung-fu", 171,
      "Yokohama Chukagai at New Year: erhu with slides, dizi flute, yangqin tremolo, lion-dance drum, gong and "
      "cymbals, woodblocks pacing the break", lift=0, reverb="orchestral")
def yokohama(B):
    g = {0: {"wbh": "x...x...x...x...", "wbl": "..x...x...x...x."},
         1: {"TK": "X.......X.......", "ch": "....x.......x..."},
         2: [{"TK": "X.xX..x.X.x...x.", "TKL": "X.......X.......", "ch": "....X.......X...", "wbh": "x.x.x.x.x.x.x.x."}],
         3: [{"k": "X.......X.......", "TK": "X.xX.xX.X.xX.xX.", "TKL": "X...X...X...X...", "ch": "..X...X...X...X.",
              "s": "....X.......X...", "wbh": "x.x.x.x.x.x.x.x."}]}
    B.drums(g, kit="orchestra", fill="taiko", gain=-6, perc_gain=-10, roll="TK")
    B.bass(G(33), {0: HOLD, 1: ROOT_FIFTH, 2: OCT8, 3: OCT8}, low=31, gain=-6)
    B.comp("yangqin", G(15), {0: [(0, 3.8, .55, "T")], 2: [(0, 1.8, .6, "T"), (2, 1.8, .6, "T")]}, 60, 79, 2, gain=-12, pan=-0.3)
    B.hook("pipa", G(106), {1, 2, 3}, gain=-10, pan=0.3)
    B.pad("strings", G(48), {3}, 55, 77, 4, gain=-15)
    B.lead([[L(G(40), -1, art="legato", orn=("slide", "grace"), name="erhu")],
            [L(G(72), -1, orn=("grace", "trill"), name="dizi")]],
           brk=[L(G(15), -1, name="yangqin_solo")],
           peak=[L(G(72), -1, orn=("grace",), name="dizi_peak"), L(G(40), -4, oct=-12, art="legato", name="erhu_under"),
                 L(G(61), -9, oct=-12, name="brass_unison")], scale="chinese")
    B.answer("dizi_answer", G(73), "chinese", lo=67, hi=91, gain=-11)
    B.motif("gong", G(48, 128), "gong_perc", perc=True, gain=-6)


@band("Germany", "Black Forest", 3, "Brüder Grimm Kammerorchester", "Fairy-tale orchestral scherzo", 171,
      "A Grimm fairy tale in the Schwarzwald: bassoon sneaks in, clarinet and oboe take over, pizzicato oompah, "
      "celesta playing the riff, horns and timpani at the peak, cuckoo clocks ticking through the woods",
      reverb="orchestral", riser=False, lift=0)
def black_forest(B):
    g = {0: {"wbh": "x.......x.......", "wbl": "....x.......x..."},
         1: {"wbh": "x.......x.......", "wbl": "....x.......x..."},
         2: [{"k": "X.......X.......", "tri": "....x.......x..."}],
         3: [{"k": "X.......X.......", "s": "....x.......x...", "C": "x...............", "timh": "X.......X......."}]}
    B.drums(g, kit="orchestra", fill="none", gain=-8, perc_gain=-11, crash=False)
    B.bass(G(45), {1: ROOT_FIFTH, 2: ROOT_FIFTH, 3: ROOT_FIFTH, 0: [(0, .3, "R", .7)]}, low=36, gain=-6, name="pizz_bass")
    oom = [(1, .2, .7, "C"), (3, .2, .7, "C")]
    B.comp("pizz_strings", G(45), {1: oom, 2: oom, 3: oom}, 55, 72, 3, gain=-10, pan=-0.25)
    B.pad("horns", G(60), {3}, 53, 70, 3, gain=-12, pan=0.2)
    B.pad("strings", G(48), {2, 3}, 57, 79, 4, gain=-15)
    B.hook("celesta", G(8), {1, 2, 3}, gain=-10, pan=0.3)
    B.lead([[L(G(70), -1, art="stacc", orn=("grace",), name="bassoon", oct=-12)],
            [L(G(71), -1, orn=("grace", "mordent"), name="clarinet")],
            [L(G(68), -1, art="legato", orn=("trill",), name="oboe")]],
           brk=[L(G(73), -1, art="legato", orn=("grace",), name="flute_alone")],
           peak=[L(G(60), -2, name="horns_lead"), L(G(73), -5, oct=12, name="flute_8va"),
                 L(G(48), -6, art="legato", name="strings_lead")], scale="harmonic")
    B.answer("flute", G(73), "harmonic", lo=67, hi=93, gain=-11, pan=0.3)
    B.motif("cuckoo", G(79), "cuckoo", octave=5, gain=-8)


@band("Scandinavia", "Helsinki", 3, "Humppa-Orkesteri Revontuli", "Finnish humppa", 200,
      "Finnish dance-hall humppa (Eläkeläiset): galloping two-beat bass, accordion offbeats, clarinet and trumpet "
      "on the tune, kantele harp in the break, brushes going hell for leather", swing=.56, lift=1, reverb="faithful")
def helsinki(B):
    g = {0: {}, 1: {"s": "..x...x...x...x."}, 2: {"k": "X...X...X...X...", "s": "..x...x...x...x."},
         3: {"k": "X...X...X...X...", "s": "..X...X...X...X.", "h": "x.x.x.x.x.x.x.x.", "tamb": "..x...x...x...x."}}
    B.drums(g, kit="brush", fill="snare", gain=-6)
    B.bass(G(32), {0: HOLD, 1: ROOT_FIFTH, 2: [(0, .5, "R", 1), (1, .5, "5", .85), (2, .5, "R", .95), (3, .5, "5", .85)],
           3: [(0, .5, "R", 1), (1, .5, "5", .85), (2, .5, "8", .95), (3, .5, "A", .85)]}, low=31, gain=-5)
    off = [(1, .3, .8, "C"), (3, .3, .8, "C")]
    B.comp("accordion_offbeat", G(21), {1: off, 2: off, 3: off + [(3.5, .2, .6, "C")]}, 55, 72, 3, gain=-9, pan=-0.3)
    B.hook("kantele", G(46), {0, 1}, gain=-11, pan=0.3)
    B.lead([[L(G(71), -1, orn=("grace",), name="clarinet")],
            [L(G(21), -1, name="accordion_lead")]],
           brk=[L(G(46), -1, name="kantele_solo")],
           peak=[L(G(56), -1, name="trumpet_peak"), L(G(71), -5, oct=12, name="clarinet_8va"),
                 L(G(21), -8, name="accordion_double")], scale="harmonic")
    B.answer("trumpet", G(56), "harmonic", lo=60, hi=82, gain=-10, pan=0.3)


@band("France", "Bordeaux", 3, "Les Violons du Château", "Baroque concerto grosso", 160,
      "Versailles meets the vineyards: harpsichord continuo, oboe and violin soloists with trills and mordents, "
      "running cello bass, a ripieno string orchestra, timpani and trumpets only at the final peak — no drums",
      reverb="orchestral", riser=False, lift=0)
def bordeaux(B):
    g = {3: {"timh": "X.......X.......", "timl": "....X.......X..."}}
    B.drums(g, kit="orchestra", fill="none", gain=-9, perc_gain=-8, crash=False, roll="timh")
    run = [(o / 2, .45, ["R", "8", "5", "8", "3", "8", "5", "A"][o], .9 if o % 2 == 0 else .7) for o in range(8)]
    B.bass(G(42), {0: HOLD, 1: ROOT_FIFTH, 2: run, 3: run}, low=36, gain=-6, name="cello_continuo")
    B.comp("harpsichord", G(6), {0: [(0, 3.8, .6, "A")], 1: [(0, 3.8, .65, "alb")], 2: [(0, 3.8, .65, "alb")],
           3: [(0, .4, .8, "C"), (1, .4, .7, "C"), (2, .4, .8, "C"), (3, .4, .7, "C")]}, 55, 76, 3, gain=-10, pan=-0.3)
    B.pad("ripieno", G(48), {2, 3}, 55, 79, 4, gain=-13, pan=0.2)
    B.hook("violin2", G(40), {1, 2, 3}, gain=-11, pan=0.35)
    B.lead([[L(G(68), -1, orn=("trill", "mordent"), name="oboe")],
            [L(G(40), -1, orn=("mordent", "grace"), name="violin_solo")]],
           brk=[L(G(6), -1, orn=("mordent",), name="harpsichord_solo")],
           peak=[L(G(56), -2, orn=("trill",), name="baroque_trumpet"), L(G(40), -4, oct=12, name="violins_8va"),
                 L(G(68), -6, harm=-2, name="oboe_3rds")], scale="harmonic")
    B.answer("violin_answer", G(40), "harmonic", lo=67, hi=91, gain=-11, pan=0.35, orn=("mordent",))


@band("Italy", "Sicily", 3, "I Pupi di Palermo", "Sicilian tarantella", 180,
      "A wild tarantella from Palermo's puppet theatre: mandolins in tremolo, accordion, clarinet with phrygian-dominant "
      "turns, tambourines everywhere, the tempo of a spider bite", swing=.62, lift=1, reverb="faithful")
def sicily(B):
    g = {0: {"tamb": "x.x.x.x.x.x.x.x."}, 1: {"tamb": "X.xX.xX.xX.xX.x.", "k": "X.......X......."},
         2: {"tamb": "X.xX.xX.xX.xX.x.", "k": "X.......X.......", "s": "....x.......x..."},
         3: {"tamb": "XxxXxxXxxXxxXxx.", "k": "X...X...X...X...", "s": "..x.X...x.x.X...", "cas": "x.xx.xx.x.xx.xx."}}
    B.drums(g, kit="standard", fill="snare", gain=-7, perc_gain=-8)
    B.bass(G(32), {0: HOLD, 1: ROOT_FIFTH, 2: [(0, .5, "R", 1), (1, .5, "5", .8), (2, .5, "R", .95), (3, .5, "5", .8)],
           3: OCT8}, low=31, gain=-5)
    B.comp("accordion", G(21), {1: [(1, .3, .7, "C"), (3, .3, .7, "C")], 2: [(1, .3, .75, "C"), (3, .3, .75, "C")],
           3: [(.5, .2, .7, "C"), (1, .3, .8, "C"), (2.5, .2, .7, "C"), (3, .3, .8, "C")]}, 55, 72, 3, gain=-10, pan=-0.3)
    B.comp("mandolin_trem", G(25), {0: [(0, 3.8, .55, "T")], 2: [(0, 1.8, .6, "T"), (2, 1.8, .6, "T")]}, 64, 81, 2, gain=-12, pan=0.3)
    B.hook("mandolin", G(25), {1, 2, 3}, gain=-10, pan=0.3)
    B.lead([[L(G(71), -1, orn=("grace", "mordent"), name="clarinet")],
            [L(G(25), -1, orn=("trem",), name="mandolin_lead")]],
           brk=[L(G(21), -1, art="legato", name="accordion_alone")],
           peak=[L(G(71), -1, orn=("grace",), name="clarinet_peak"), L(G(40), -5, oct=12, name="violin_8va"),
                 L(G(21), -7, name="accordion_double")], scale="phrygdom")
    B.answer("accordion_turns", G(21), "phrygdom", lo=60, hi=84, gain=-11)


@band("UK", "Loch Ness", 3, "The Pipes of Urquhart", "Highland pipe band / ceilidh reel", 168,
      "A Highland pipe band on the loch: pipes with cut grace notes over a drone, pipe-snare rolls and tenor drums; "
      "the ceilidh band (fiddle, tin whistle, bodhrán) takes the second pass, Celtic harp ripples in the misty break",
      reverb="orchestral", lift=0)
def loch_ness(B):
    g = {0: {}, 1: {"t5": "X.......X.......", "s": "xgxgxgxgxgxgxgxg"},
         2: [{"k": "X.......X.......", "t5": "X...x...X...x...", "s": "xgxgXgxgxgxgXgxg"}],
         3: [{"k": "X...X...X...X...", "t5": "X..xX...X..xX...", "s": "xgxgXgxgxgxgXgxX", "t3": "..x...x...x...x."}]}
    B.drums(g, kit="orchestra", fill="snare", gain=-6)
    B.drone("pipe_drone", G(109), {1, 2, 3}, intervals=(0, 7), octave=3, gain=-17)
    B.bass(G(32), {2: ROOT_FIFTH, 3: ROOT_FIFTH}, low=33, gain=-8)
    B.comp("guitar_ceilidh", G(25), {2: [(0, .3, .8, "S"), (1, .3, .6, "S"), (1.5, .3, .7, "S"), (2, .3, .8, "S"), (3, .3, .6, "S"), (3.5, .3, .7, "S")]},
           55, 72, 4, gain=-11, pan=-0.3)
    B.pad("strings", G(48), {3}, 55, 76, 4, gain=-16)
    B.hook("celtic_harp", G(46), {0, 1, 2}, gain=-10, pan=0.3)
    B.lead([[L(G(109), -2, orn=("cut", "grace"), name="bagpipes")],
            [L(G(110), -1, orn=("cut", "trill"), name="fiddle"), L(G(78), -7, oct=12, orn=("cut",), name="tin_whistle")]],
           brk=[L(G(46), -1, name="harp_alone")],
           peak=[L(G(109), -2, orn=("cut",), name="pipes_peak"), L(G(110), -5, name="fiddle_unison"),
                 L(G(78), -8, oct=12, name="whistle_8va")], scale="dorian")
    B.answer("whistle", G(78), "dorian", lo=67, hi=91, gain=-11, orn=("cut",))


# ================================================================== TUNE 4 (Frankfurt melody, G minor)
@band("USA", "San Francisco", 4, "Bullitt Street Chase Band", "Lalo-Schifrin jazz-rock chase", 152,
      "Steve McQueen's Mustang over Russian Hill: walking bass, jazz-guitar comps, flute then brass on the melody, "
      "vibes on the arp, muted-trumpet blues answers, bongos, the cable-car bell clanging", swing=.58, lift=1)
def san_francisco(B):
    g = {0: {"R": "x...x.x.x...x.x."}, 1: {"R": "X...X.x.X...X.x.", "p": "....x.......x...", "bongo": "x.x...x.x.x...x."},
         2: [{"R": "X...X.x.X...X.x.", "p": "....x.......x...", "k": "x.....x...x.....", "s": "....g..x....g...",
              "bongo": "x.x...x.x.x...x."}],
         3: [{"k": "X.....x...X..x..", "s": "....X..g....X..g", "h": "x.x.x.x.x.x.x.x.", "bongo": "x.xx..x.x.xx..x.",
              "lbongo": "...x.......x...."}]}
    B.drums(g, kit="jazz", fill="toms", gain=-5, perc_gain=-10)
    B.bass(G(32), {0: HOLD, 1: WALK, 2: WALK, 3: [(0, .75, "R", 1), (1, .5, "5", .8), (1.5, .5, "8", .8), (2.5, .75, "R", .9), (3.5, .5, "A", .8)]},
           low=31, gain=-4)
    jc = [(0, .3, .7, "C"), (1.5, .4, .65, "C"), (3, .3, .6, "C")]
    B.comp("jazz_guitar", G(26), {1: jc, 2: jc, 3: jc}, 52, 70, 4, ext="jazz", gain=-10, pan=-0.3)
    B.comp("brass_hits", G(61), {3: [(0, .3, .95, "C"), (1.5, .3, .85, "C"), (2.5, .5, .9, "C")]}, 58, 77, 4, ext="jazz",
           gain=-11, pan=0.25)
    B.hook("vibes", G(11), {1, 2}, gain=-11, pan=0.3)
    B.lead([[L(G(73), -1, art="legato", orn=("grace", "fall"), name="flute")],
            [L(G(66), -1, art="legato", orn=("slide",), name="tenor_sax"), L(G(73), -8, oct=12, name="flute_8va")]],
           brk=[L(G(11), -1, name="vibes_solo")],
           peak=[L(G(61), -1, name="brass_peak"), L(G(56), -5, oct=12, orn=("fall",), name="trumpet_8va"),
                 L(G(57), -7, oct=-12, name="bones")], scale="dorian")
    B.answer("muted_trumpet", G(59), "blues", lo=60, hi=82, gain=-9, pan=0.3)
    B.motif("cable_car", G(14), "cable_car", octave=5, gain=-8)


@band("South America", "Rain Forest", 4, "Carimbó do Amazonas", "Amazonian carimbó", 140,
      "A riverboat carimbó from Pará: curimbó drums, maracas, banjo offbeat strums, clarinet and flute melodies, "
      "marimba hook, birds calling over the canopy in the break", swing16=.56, lift=0, reverb="faithful")
def rain_forest(B):
    g = {0: {"mar": "x.x.x.x.x.x.x.x."},
         1: {"lconga": "X.......X.......", "mar": "xgxgxgxgxgxgxgxg"},
         2: [{"lconga": "X..x....X..x....", "conga": "....x.x.....x.x.", "mar": "xgxgxgxgxgxgxgxg", "k": "X.......X......."}],
         3: [{"lconga": "X..x..x.X..x..x.", "conga": "..x.x.x...x.x.xx", "mconga": ".x.....x.x......", "mar": "xgxgxgxgxgxgxgxg",
              "k": "X.......X.......", "s": "....x.......x..."}]}
    B.drums(g, kit="standard", fill="latin", gain=-7, perc_gain=-7, crash=False)
    cb = [(0, .75, "R", 1), (1, .5, "5", .8), (2, .75, "R", .95), (3, .5, "5", .8), (3.5, .5, "A", .7)]
    B.bass(G(33), {0: HOLD, 1: ROOT_FIFTH, 2: cb, 3: cb}, low=31, gain=-5)
    bj = [(.5, .2, .8, "U"), (1.5, .2, .8, "U"), (2.5, .2, .8, "U"), (3.5, .2, .8, "U")]
    B.comp("banjo", G(105), {1: bj, 2: bj, 3: bj}, 60, 76, 3, gain=-11, pan=-0.3)
    B.pad("jungle_pad", G(88), {0, 1}, 55, 74, 4, gain=-17)
    B.hook("marimba", G(12), {1, 2, 3}, gain=-10, pan=0.3)
    B.fx("birds", G(123), 72, levels=(0, 1), every=4, gain=-17)
    B.lead([[L(G(71), -1, orn=("grace",), name="clarinet")],
            [L(G(73), -1, art="legato", orn=("grace", "trill"), name="flute")]],
           brk=[L(G(75), -1, art="legato", name="pan_flute")],
           peak=[L(G(71), -1, orn=("grace",), name="clarinet_peak"), L(G(73), -5, oct=12, name="flute_8va"),
                 L(G(66), -8, oct=-12, name="sax_under")], push=True, scale="dorian")
    B.answer("flute_answer", G(73), "minpent", lo=67, hi=91, gain=-11, pan=0.3)


@band("Japan", "Kyoto", 4, "Gion Lo-Fi Collective", "Lo-fi hip-hop with koto", 88,
      "Late night in Gion: dusty boom-bap with brushes, Rhodes minor-9ths, fretless bass, koto and shakuhachi trading "
      "the melody, kalimba on the arp, a temple bell at midnight", swing16=.62, reverb="faithful", riser=False)
def kyoto(B):
    g = {0: {"shk": "..x...x...x...x."}, 1: {"k": "X.........x.....", "rim": "....x.......x...", "h": "x.x.x.x.x.x.x.x."},
         2: [{"k": "X.........x.....", "s": "....X.......X...", "h": "x.xgx.x.x.xgx.x."},
             {"k": "X.......x.x.....", "s": "....X.......X..g", "h": "x.x.x.xgx.x.x.x."}],
         3: [{"k": "X.....x...x..x..", "s": "....X..g....X...", "h": "xgx.xgx.xgx.xgx.", "shk": "..x...x...x...x."}]}
    B.drums(g, kit="brush", fill="none", gain=-5, crash=False)
    lb = [(0, 1.5, "R", 1), (1.75, .25, "5", .6), (2, 1.25, "5", .85), (3.5, .5, "A", .7)]
    B.bass(G(35), {0: HOLD, 1: lb, 2: lb, 3: lb}, low=31, gain=-4, name="fretless")
    rh = [(0, 1.75, .6, "C"), (2.5, 1.25, .5, "C")]
    B.comp("rhodes", G(4), {0: HOLDC, 1: rh, 2: rh, 3: rh}, 55, 74, 5, ext="soul", gain=-9, pan=-0.2, ant="even")
    B.hook("kalimba", G(108), {1, 2, 3}, gain=-11, pan=0.3)
    B.lead([[L(G(107), -1, orn=("slide",), name="koto")],
            [L(G(77), -1, art="legato", orn=("slide",), name="shakuhachi")]],
           brk=[L(G(4), -2, name="rhodes_alone")],
           peak=[L(G(107), -1, name="koto_peak"), L(G(77), -6, oct=-12, art="legato", name="shakuhachi_under")],
           push=True, scale="hira")
    B.answer("koto_answer", G(107), "hira", lo=60, hi=84, gain=-11)
    B.motif("temple_bell", G(14), "temple", octave=3, gain=-8, where=[0])


@band("Germany", "Frankfurt", 4, "Mainhattan Trance Project", "Euro-trance", 138,
      "Frankfurt's 90s trance capital (Sven Väth / Dance Valley): gated supersaw 16ths, rolling offbeat bass, "
      "Moog lead, a bare piano breakdown, snare-roll build and a supersaw anthem drop a tone up", lift=2, reverb="synthwave")
def frankfurt(B):
    g = {0: {"h": "..x...x...x...x."}, 1: {"k": "X...X...X...X...", "o": "..x...x...x...x."},
         2: [{"k": "X...X...X...X...", "o": "..x...x...x...x.", "clap": "....X.......X...", "h": "xgxgxgxgxgxgxgxg"}],
         3: [{"k": "X...X...X...X...", "o": "..X...X...X...X.", "clap": "....X.......X...", "h": "xgxgxgxgxgxgxgxg",
              "s": "..........g....."}]}
    B.drums(g, kit="808", fill="snare", gain=-3)
    roll = [(o / 4, .2, "R", .9 if o % 4 == 2 else .6) for o in range(16) if o % 4 != 0]
    B.bass(S("Basses/Bass 2.fxp"), {0: HOLD, 1: OFFBASS, 2: roll, 3: roll}, low=33, gain=-3, name="rolling_bass")
    gate = [(o / 4, .15, .85 if o % 2 == 0 else .55, "C") for o in range(16)]
    B.comp("gated_supersaw", S("Polysynths/Super Jupiter.fxp"), {2: gate, 3: gate}, 60, 79, 3, gain=-14, pan=0.2)
    B.comp("piano", G(0), {0: [(0, 3.8, .6, "C")]}, 55, 76, 4, gain=-9)
    B.pad("mks_pad", S("Pads/MKS-70 Warm Pad.fxp"), {0, 1, 2}, 55, 74, 4, gain=-15)
    B.hook("pluck_arp", S("Plucks/The 1980s.fxp"), {1, 2, 3}, gain=-11, pan=-0.3, delay=.25)
    B.lead([[L(S("Leads/Moogy Saw.fxp"), -1, art="legato", name="moog_lead", delay=.15)],
            [L(S("Leads/Sync Lead.fxp"), -1, art="legato", name="sync_lead", delay=.15)]],
           brk=[L(G(0), -1, name="piano_breakdown")],
           peak=[L(S("Polysynths/Super Jupiter.fxp"), -1, name="supersaw_anthem", delay=.15),
                 L(S("Leads/Moogy Saw.fxp"), -6, oct=12, art="legato", name="moog_8va", delay=.15)])


@band("Scandinavia", "Oslo", 4, "Hall of the Mountain King Philharmonic", "Grieg troll march", 156,
      "Peer Gynt in the fjords: bassoons and pizzicato creep in, oboe and strings grow, brass and timpani explode, "
      "a Hardanger fiddle plays over a drone in the break, bronze lur horns at the start", reverb="orchestral",
      riser=False, lift=0)
def oslo(B):
    g = {1: {"timl": "X.......X......."}, 2: {"timh": "X...x...X...x...", "k": "X.......X......."},
         3: {"timh": "X.x.X.x.X.x.X.x.", "timl": "X.......X.......", "C": "X.......X.......", "s": "....X.......X..."}}
    B.drums(g, kit="orchestra", fill="snare", gain=-7, perc_gain=-7, crash=False, roll="timh")
    B.bass(G(45), {1: [(0, .4, "R", .8), (1, .4, "5", .7), (2, .4, "R", .8), (3, .4, "5", .7)],
           2: [(o / 2, .4, "R" if o % 2 == 0 else "5", .85) for o in range(8)],
           3: [(o / 2, .45, "R" if o % 2 == 0 else "8", .95) for o in range(8)]}, low=29, gain=-5, name="pizz_contrabass")
    B.drone("hardanger_drone", G(92), {0}, intervals=(0, 7), octave=3, gain=-15)
    B.pad("strings", G(48), {2, 3}, 53, 77, 4, gain=-12)
    B.pad("brass", G(61), {3}, 50, 70, 4, gain=-12, pan=0.2)
    B.hook("glock", G(9), {2}, gain=-14, pan=0.3)
    B.lead([[L(G(70), -1, art="stacc", name="bassoon", oct=-12), L(G(45), -6, name="pizz_unison")],
            [L(G(68), -1, orn=("grace",), name="oboe"), L(G(40), -5, art="legato", name="violins")]],
           brk=[L(G(110), -1, orn=("trill", "grace"), name="hardanger_fiddle")],
           peak=[L(G(48), -1, art="legato", name="tutti_strings"), L(G(61), -3, oct=-12, name="tutti_brass"),
                 L(G(73), -6, oct=12, name="piccolo_8va")], scale="harmonic")
    B.answer("horn_calls", G(60), "harmonic", lo=55, hi=74, gain=-10)
    B.motif("lur", G(60), "lur", octave=3, gain=-6, where=[0])


@band("France", "Monaco", 4, "Grand Prix Filter House", "French house", 124,
      "Daft Punk / Cassius on the Monte-Carlo harbour: filtered disco strings, funky octave bass, vocoder voice, "
      "Sync lead, claps, and a Grand-Prix fanfare at the start line", swing16=.55, reverb="synthwave", lift=0)
def monaco(B):
    g = {0: {"h": "..x...x...x...x."}, 1: {"k": "X...X...X...X...", "o": "..x...x...x...x."},
         2: [{"k": "X...X...X...X...", "o": "..x...x...x...x.", "clap": "....X.......X...", "h": "xgxgxgxgxgxgxgxg"}],
         3: [{"k": "X...X...X...X...", "o": "..x...x...x...x.", "clap": "....X.......X...", "h": "xgxgxgxgxgxgxgxg",
              "tamb": "....x.......x...", "shk": "xgxgxgxgxgxgxgxg"}]}
    B.drums(g, kit="808", fill="snare", gain=-4)
    fb = [(0, .25, "R", 1), (.5, .25, "8", .8), (1, .25, "R", .9), (1.5, .25, "8", .8), (1.75, .25, "R", .6),
          (2.5, .25, "8", .8), (3, .25, "R", .9), (3.5, .25, "8", .85)]
    B.bass(S("Basses/Rubber Bass.fxp"), {0: HOLD, 1: OCT8, 2: fb, 3: fb}, low=33, gain=-3, name="funky_bass")
    ds = [(0, .5, .8, "C"), (.75, .5, .7, "C"), (1.5, .5, .75, "C"), (2.5, .25, .7, "C"), (3, .75, .8, "C")]
    B.comp("disco_strings", G(48), {1: ds, 2: ds, 3: ds}, 60, 81, 4, ext="soul", gain=-11, pan=-0.2)
    B.comp("rhodes", G(4), {0: HOLDC}, 55, 74, 5, ext="soul", gain=-10)
    B.hook("pluck", S("Plucks/Hasselhoff.fxp"), {1, 2, 3}, gain=-12, pan=0.3, delay=.25)
    B.lead([[L(S("Leads/Sync Lead.fxp"), -1, art="legato", name="sync_lead", delay=.12)],
            [L(G(54), -2, art="legato", name="vocoder"), L(S("Leads/Sync Lead.fxp"), -8, oct=-12, name="sync_under", delay=.12)]],
           brk=[L(G(54), -1, art="legato", name="vocoder_alone")],
           peak=[L(S("Leads/Sync Lead.fxp"), -1, art="legato", name="sync_peak", delay=.12),
                 L(G(54), -6, oct=12, name="vocoder_8va"), L(G(61), -9, oct=-12, name="brass_under")], push=True, scale="dorian")
    B.answer("voice", G(54), "dorian", lo=60, hi=79, gain=-11)
    B.motif("fanfare", G(61), "fanfare", octave=4, gain=-8, where=[0])


@band("Italy", "Florence", 4, "Consort dei Medici", "Renaissance consort", 150,
      "A Medici court consort: recorders with trills, shawm (oboe), lute strumming, viola da gamba bass, harpsichord "
      "on the arp, tabor and tambourine — a Renaissance galliard of the race theme", reverb="orchestral", riser=False)
def florence(B):
    g = {1: {"t3": "X.....x.X.......", "tamb": "....x.......x..."}, 2: {"t3": "X.....x.X...x...", "tamb": "..x...x...x...x."},
         3: {"t3": "X.x...x.X.x.x...", "tamb": "x.x.x.x.x.x.x.x.", "t5": "X.......X......."}}
    B.drums(g, kit="standard", fill="none", gain=-8, perc_gain=-10, crash=False, roll="t3")
    B.bass(G(42), {0: HOLD, 1: ROOT_FIFTH, 2: ROOT_FIFTH, 3: [(0, .5, "R", .9), (1, .5, "5", .8), (2, .5, "R", .9), (3, .5, "A", .8)]},
           low=36, gain=-6, name="viola_da_gamba")
    lute = [(0, .4, .8, "S"), (1, .3, .6, "S"), (1.5, .3, .6, "S"), (2, .4, .8, "S"), (3, .4, .7, "S")]
    B.comp("lute", G(24), {0: HOLDC, 1: lute, 2: lute, 3: lute}, 52, 70, 4, gain=-8, pan=-0.3)
    B.pad("viols", G(48), {3}, 55, 74, 4, gain=-16)
    B.hook("harpsichord", G(6), {1, 2, 3}, gain=-10, pan=0.3)
    B.lead([[L(G(74), -1, orn=("trill", "mordent"), name="recorder")],
            [L(G(68), -1, orn=("mordent",), name="shawm")]],
           brk=[L(G(24), -1, name="lute_solo")],
           peak=[L(G(68), -1, name="shawm_peak"), L(G(74), -5, oct=12, orn=("trill",), name="recorder_8va"),
                 L(G(57), -7, oct=-12, name="sackbut")], scale="dorian")
    B.answer("recorder_answer", G(74), "dorian", lo=67, hi=91, gain=-11, orn=("mordent",))


@band("UK", "Stonehenge", 4, "Druids of Salisbury Plain", "Pagan folk", 156,
      "Solstice at the stones: tin whistle with cuts, hurdy-gurdy over a drone, frame drums, Celtic harp on the arp, "
      "choir and strings swelling at the peak, a horn call from the barrow mounds", reverb="orchestral", lift=0)
def stonehenge(B):
    g = {0: {}, 1: {"t5": "X.....x.X.......", "t3": "......x.......x."},
         2: [{"t5": "X.....x.X...x...", "t3": "...x..x....x..x.", "tamb": "....x.......x..."}],
         3: [{"k": "X.......X.......", "t5": "X.....x.X...x...", "t3": "...x..x....x..xx", "s": "....X.......X...",
              "tamb": "..x...x...x...x."}]}
    B.drums(g, kit="orchestra", fill="toms", gain=-6, perc_gain=-9)
    B.drone("gurdy_drone", G(92), {1, 2, 3}, intervals=(0, 7), octave=3, gain=-15)
    B.bass(G(32), {2: ROOT_FIFTH, 3: [(0, .75, "R", 1), (1, .5, "5", .8), (2, .75, "R", .95), (3, .5, "8", .8)]}, low=31, gain=-6)
    B.pad("choir", G(52), {3}, 55, 76, 4, gain=-12)
    B.pad("strings", G(49), {2, 3}, 53, 77, 4, gain=-15)
    B.hook("celtic_harp", G(46), {0, 1, 2, 3}, gain=-10, pan=0.3)
    B.lead([[L(G(78), -1, orn=("cut", "grace"), name="tin_whistle")],
            [L(G(110), -1, art="legato", orn=("grace",), name="hurdy_gurdy")]],
           brk=[L(G(46), -1, name="harp_alone")],
           peak=[L(G(52), -2, art="legato", name="choir_lead"), L(G(78), -5, oct=12, orn=("cut",), name="whistle_8va"),
                 L(G(110), -6, name="gurdy_unison")], scale="dorian")
    B.answer("whistle_answer", G(78), "dorian", lo=67, hi=91, gain=-11, orn=("cut",))
    B.motif("horn_call", G(60), "lur", octave=3, gain=-7, where=[0])
