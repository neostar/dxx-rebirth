# shellcheck shell=bash
# The frame hash lines rtrace logs for the committed synthetic traces, as
# Apple's GL draws them (M1 Max, macOS 26.7); sourced by rtrace-tests.sh
# (R1-style GL pins and R3 on Metal) and rtrace-d3d12.sh (R3 on D3D12).
# The sourcing script sets root, the repository's top directory.
# shellcheck disable=SC2034,SC2154 # set and read by the sourcing scripts

# The sample: frame 1 is 64x48 pixels of (255, 255, 0), hashed with 64-bit
# FNV-1a by python3 when written; frame 0, the shaded triangle.
expected=('0 64x48 86388f1d5003af2a' '1 64x48 9f40fbb36ef64325')
sample=$root/common/render/rtrace/synthetic/sample.rtr
# The overbright trace (#27).
expected_overbright=('0 64x48 5d1b6b445fb7f05a' '1 64x48 418a435cc9c67e56')
overbright=$root/common/render/rtrace/synthetic/overbright.rtr
# The flash trace (#30).
expected_flash=('0 64x48 a088becd21e46f87' '1 64x48 4142e7869f743d59' '2 64x48 c33733e407fec502')
flash=$root/common/render/rtrace/synthetic/flash.rtr
# The walls trace (#28).
expected_walls=('0 64x48 75020e289509a104' '1 64x48 01416b4d8a73c8c9' '2 64x48 58f167d16748f0ab')
walls=$root/common/render/rtrace/synthetic/walls.rtr
# The alpha trace (#29).
expected_alpha=('0 64x48 184a14cee6aceb25' '1 64x48 2eea67c587ee7f25' '2 64x48 891d9777e7a7e925' '3 64x48 a80644d0e3157625' '4 64x48 e2bd10483a9fad25' '5 64x48 af7964e69a3f7925')
alpha=$root/common/render/rtrace/synthetic/alpha.rtr
# The lines trace (#32): GL's frames, then SDL_GPU's on Metal.  Frames 1 and 2
# are the same; frames 0, 3 and 4 differ by a few pixels at the ends of lines, which
# test-sdlgpu holds within the tolerance of ADR 2's pixel-centre rule.
expected_lines=('0 64x48 a5d12a818955d06e' '1 64x48 26915db7ed30cd45' '2 64x48 9000b78311b0898e' '3 64x48 14d868bc00457065' '4 64x48 96e0fb1fb76d65bd')
expected_lines_metal=('0 64x48 c523f67cb3e03eb2' '1 64x48 26915db7ed30cd45' '2 64x48 9000b78311b0898e' '3 64x48 73ce0ff774ec2268' '4 64x48 dc4a426ae6920903')
lines_rtr=$root/common/render/rtrace/synthetic/lines.rtr
# The oversize trace (#18): viewports past the target, which SDL_GPU clamps
# to GL's pixels.
expected_oversize=('0 64x48 d7b92cb41fbcf729' '1 64x48 dbd8d0810c396d39')
oversize=$root/common/render/rtrace/synthetic/oversize.rtr
# The fonts trace (#33): the ramps of the font formats (frames 0-7; the formats are
# not quantized, so frames 2-6 repeat frames 0 and 1) and text in them (8-10).
expected_fonts=('0 64x48 49e2bd63b6ae0b25' '1 64x48 e3483f86e1fe7b25' '2 64x48 49e2bd63b6ae0b25' '3 64x48 e3483f86e1fe7b25' '4 64x48 e3483f86e1fe7b25' '5 64x48 e3483f86e1fe7b25' '6 64x48 e3483f86e1fe7b25' '7 64x48 51a3b262cedf3725' '8 64x48 d4e0fa5984a7c7e5' '9 64x48 9514bc61e4b568fd' '10 64x48 9514bc61e4b568fd')
fonts=$root/common/render/rtrace/synthetic/fonts.rtr
# The filters trace (#34): minified textures under the classic, upscale and trilinear
# filters, each without and with anisotropy.
expected_filters=('0 64x48 abb6708701beb951' '1 64x48 bf8cff063215a44d' '2 64x48 dd52b4e17b9124c9' '3 64x48 fb84893daf27c17a' '4 64x48 acf0c45d32ada5a5' '5 64x48 5c57a490f2fa9148')
# SDL_GPU's on Metal, with GL's mipmaps: within one level of GL's in every
# filtered frame, and 5 pixels off in the classic one (ADR 2's FLIP per filter).
expected_filters_metal=('0 64x48 0d33b500a6d9edb0' '1 64x48 3c3b6542f0892545' '2 64x48 94b7bb5bbc66bd40' '3 64x48 6296affa12046ccd' '4 64x48 5a2c8b8fdd1d6b6c' '5 64x48 5ec9c859bccd8b27')
# How far each of those frames may be off GL's, "PIXELS LEVELS": at most PIXELS pixels
# differ, none by more than LEVELS in a channel. Measured on Metal (#34): frame 0, classic,
# takes other minified nearest texels on 5 pixels; the filtered frames are within one level
# on 2, 15 and 38 pixels.
filters_gl_tolerance=('5 255' '2 1' '15 1' '38 1' '15 1' '38 1')
filters=$root/common/render/rtrace/synthetic/filters.rtr
# The overlay trace (#31).
overlay=$root/common/render/rtrace/synthetic/overlay.rtr
