# Aa/Shift press and release — image 54

Root cause: CP32 interpreted TCA8418 event bit 7 backwards. It treated press
as release, emitted printable characters on release, and activated modifiers
when released. This could leave uppercase active after tapping Aa.

MINIX `minix-2.0.0/src/kernel/keyboard.c:make_break` maintains modifier state
on press/release, but the PC scancode break bit has the opposite polarity.
The Cardputer decoder must preserve the behavior, not that bit interpretation.

Hardware reference: M5Stack's official
[TCA8418 reader](https://github.com/m5stack/M5Cardputer/blob/master/src/utility/Keyboard/KeyboardReader/TCA8418.cpp)
uses bit 7 set to add a pressed key and clear to remove it. Its
[key map](https://github.com/m5stack/M5Cardputer/blob/master/src/utility/Keyboard/Keyboard.h)
places Fn at row 2/column 0 and Aa/Shift at row 2/column 1. These correspond to
event numbers 3 and 7 with the published remapping. The mapping itself stays
unchanged; CP32 now applies the correct event polarity.

Image 54 emits printable keys on press, updates Shift/Ctrl/Fn/Alt on both
edges, and clears Shift when Aa is released. Fn does not select uppercase;
its alternate function layer remains unimplemented. Ctrl-Shift-letter retains
the same control byte as Ctrl-letter. Invalid coordinates outside the configured
7x8 matrix are rejected rather than aliasing a valid key. No latch/caps-lock
behavior is introduced.

The existing host tests had encoded the same reversed polarity. They now
generate real press events and cover 20 lowercase/uppercase/lowercase cycles,
both key-release ordering and Fn isolation, shifted punctuation, Ctrl-Shift
and invalid events. All 23 scripts and the clean firmware build pass.

Hardware check after `[TEST KBD-SHIFT 54]`:

1. Type `a`, hold Aa and type `a`, release Aa and type `a`: expect `aAa`.
2. Hold Fn and type `a`: expect lowercase `a`, not uppercase.
3. Hold Aa for `-`: expect `_`; release Aa and repeat: expect `-`.
4. Type `cat README` using held Aa for the filename, release it, then type
   `ls` or `disk`: the command must remain lowercase.

Repeat several times, including releasing Aa before releasing a letter.
The correction is source-verified and host-tested; hardware acceptance of
image 54 is pending. FIFO overflow/lost-event recovery is not added here.
