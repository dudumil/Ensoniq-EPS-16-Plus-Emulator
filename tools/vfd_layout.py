# Annunciator legends of the ASR-10 VFD: (text, boxed, group, index, x1, x2) per row.
# x1/x2 are measured on a photo of the display in which the character pitch is 90.5 px and
# character 0 starts at x = 20. group 0 = panel commands $77/$78/$79, group 1 = $7A/$7B/$7C.
ROWS = [
  [("LOAD",1,0,0xf,45,215),("INST",0,0,0xe,245,375),("MIDI",0,0,0x3,400,530),("SYSTEM",0,0,0xc,545,785),("LAYER",0,0,0xb,805,995),
   ("ENV",0,1,0xf,1110,1225),("ODUB",1,1,0xe,1240,1415),("REC",1,1,0x3,1440,1570),("PLAY",0,1,0xc,1600,1745),("STOP",0,1,0xb,1775,1930)],
  [("CMD",1,0,0xd,45,215),("SEQ",0,0,0x2,225,350),("SONG",0,0,0x4,380,550),("PITCH",0,0,0xa,585,765),("FILTER",0,0,0x6,795,990),
   ("AMP",0,1,0xd,1080,1205),("SONG",0,1,0x2,1240,1405),("SEQ",0,1,0x4,1450,1570),("STEP",0,1,0xa,1605,1755),("REP",0,1,0x6,1810,1925)],
  [("EDIT",1,0,0x5,45,215),("MACRO",0,0,0x1,220,435),("BANK",0,0,0x7,460,620),("LFO",0,0,0x9,655,770),("WAVE",0,0,0x8,800,955),
   ("TRACK",0,1,0x5,1140,1330),("BAR",0,1,0x1,1405,1520),("BEAT",0,1,0x7,1590,1720),("CLOCK",0,1,0x9,1725,1950)],
]
ROW_Y = [45, 98, 152]   # row centres on the same photo
