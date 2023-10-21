;-----------------------------------------------------------------------------
; Tape-based Floppy Disk Formatter
; TRS-80 Model III 16K or more with FDC
; Assembles with zmac (http://48k.ca/zmac.html)
;-----------------------------------------------------------------------------
; This software is in the public domain.  No warranty expressed nor implied.
;-----------------------------------------------------------------------------
; 2021-03-25 A. Chana       Initial Release
; 2021-03-29 A. Chana       Allow 1 cylinder minimum.
; 2021-04-01 A. Chana       Added write precompensation start cylinder.
; 2021-04-02 A. Chana       Added operation selection.
;-----------------------------------------------------------------------------

; constants
def_interleave  equ     2
def_firstsec    equ     0
fm_sectors      equ     10
mfm_sectors     equ     18
min_cylinders   equ      1
max_cylinders   equ     96
def_cylinders   equ     40
def_wrtprecyl   equ     25
delay_303ms     equ     20480
delay_20ms      equ     1360
max_pre_idx_gap equ     1024

; keyboard codes
key_break       equ     0x01
key_enter       equ     0x0d
key_d           equ     'd'
key_f           equ     'f'
key_r           equ     'r'
key_s           equ     's'
key_w           equ     'w'

; IDAM offsets
fm_idam_cyl     equ     7
fm_idam_side    equ     8
fm_idam_sector  equ     9
mfm_idam_cyl    equ     16
mfm_idam_side   equ     17
mfm_idam_sector equ     18

; fdc i/o ports
irqreg          equ     0xe4
cmdstatreg      equ     0xf0
trackreg        equ     0xf1
sectorreg       equ     0xf2
datareg         equ     0xf3
drvselreg       equ     0xf4

; fdc status bits
busy            equ     0x01
index           equ     0x02
data_req        equ     0x02
track0          equ     0x04
lost_data       equ     0x04
crc_error       equ     0x08
seek_error      equ     0x10
record_nf       equ     0x10
head_loaded     equ     0x20
record_type     equ     0x20
write_fault     equ     0x20
write_prot      equ     0x40
not_ready       equ     0x80

; fdc commands, no head load and 30mS step
restore         equ     0x03                ; no verify track
seek            equ     0x17                ; verify track
step            equ     0x33                ; no verify track
stepin          equ     0x53                ; no verify track
stepout         equ     0x73                ; no verify track
readsector_s0   equ     0x82                ; single record, side 0, no delay, side compare
readsector_s1   equ     0x8a                ; single record, side 1, no delay, side compare
writesector_s0  equ     0xa2                ; single record, side 0, no delay, side compare, data mark
writesector_s1  equ     0xaa                ; single record, side 1, no delay, side compare, data mark
writesector_s0d equ     0xa3                ; single record, side 0, no delay, side compare, deleted data mark
writesector_s1d equ     0xab                ; single record, side 1, no delay, side compare, deleted data mark
readaddress     equ     0xc0                ; no delay
readtrack       equ     0xe0                ; no delay
writetrack      equ     0xf0                ; no delay
forceint        equ     0xd0                ; terminate with no interrupt

; bits in irqreg
mot_timeout     equ     0x40                ; set to enable motor timeout interrupt, read to get state
fdc_irq         equ     0x80                ; set to enable fdc interrupt, read to get irq state

; bits in drvselreg
drive0          equ     0x01                ; select drive 0
drive1          equ     0x02                ; select drive 1
drive2          equ     0x04                ; select drive 2
drive3          equ     0x08                ; select drive 3
side1           equ     0x10                ; 0 = side 0, 1 = side 1
writeprecomp    equ     0x20                ; 0 = disable write precomp, 1 = enable write precomp
genwait         equ     0x40                ; 0 = no waits, 1 = generate waits
mfm             equ     0x80                ; 0 = FM, 1 = MFM

; memory addresses
vidram          equ     3c00h               ; address of video RAM

; ROM calls
vdchar          equ     0033h               ; display character in A to the video
kbline          equ     0040h               ; wait for a line from the keyboard
kbwait          equ     0049h               ; wait for a keyboard character
delay           equ     0060h               ; delay for 2.46 + (14.8 * bc) uS
initio          equ     0069h               ; initialize all I/O
vdcls           equ     01c9h               ; clear the screen
vdline          equ     021bh               ; display a line pointed to by HL (terminated by 03h or 0dh)
ready           equ     1a19h               ; jump to BASIC ready prompt
date            equ     3033h               ; get the date as MM/DD/YY in buffer pointed to by HL
time            equ     3036h               ; get the time as HH:MM:SS in buffer pointed to by HL
