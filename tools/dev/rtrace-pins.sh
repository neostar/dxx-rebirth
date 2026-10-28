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
