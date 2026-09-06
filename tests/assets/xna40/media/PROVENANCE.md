# Synthetic media corpus (generated)

Written by `tools/xna-pipeline-oracle/media/make-media-fixtures.sh`. Every file is
synthesized from a mathematical signal by FFmpeg; none is third-party content, and none
was downloaded. Regenerate with the same FFmpeg to get the same bytes.

```
ffmpeg version 7.1.5-0+deb13u1 Copyright (c) 2000-2026 the FFmpeg developers
```

| File | How it was made |
|---|---|
| `tone_mono_44100.wav` | `ffmpeg -f lavfi -i sine=frequency=440:sample_rate=44100:duration=0.5 -ac 1 -c:a pcm_s16le tone_mono_44100.wav` |
| `tone_stereo_44100.wav` | `ffmpeg -f lavfi -i sine=frequency=440:sample_rate=44100:duration=0.5 -f lavfi -i sine=frequency=660:sample_rate=44100:duration=0.5 -filter_complex [0:a][1:a]join=inputs=2:channel_layout=stereo[a] -map [a] -c:a pcm_s16le tone_stereo_44100.wav` |
| `tone_mono_22050.wav` | `ffmpeg -f lavfi -i sine=frequency=440:sample_rate=22050:duration=0.5 -ac 1 -c:a pcm_s16le tone_mono_22050.wav` |
| `mp3_mono_44100_128k.mp3` | `ffmpeg -i tone_mono_44100.wav -c:a libmp3lame -b:a 128k -write_xing 0 -id3v2_version 0 mp3_mono_44100_128k.mp3` |
| `mp3_stereo_44100_192k.mp3` | `ffmpeg -i tone_stereo_44100.wav -c:a libmp3lame -b:a 192k -write_xing 0 -id3v2_version 0 mp3_stereo_44100_192k.mp3` |
| `mp3_mono_22050_64k.mp3` | `ffmpeg -i tone_mono_22050.wav -c:a libmp3lame -b:a 64k -write_xing 0 -id3v2_version 0 mp3_mono_22050_64k.mp3` |
| `mp3_mono_44100_tagged.mp3` | `ffmpeg -i tone_mono_44100.wav -c:a libmp3lame -b:a 128k -id3v2_version 3 -metadata title=CnaTone -metadata artist=CNA mp3_mono_44100_tagged.mp3` |
| `mp3_mono_44100_vbr.mp3` | `ffmpeg -i tone_mono_44100.wav -c:a libmp3lame -q:a 4 -id3v2_version 0 mp3_mono_44100_vbr.mp3` |
| `mp3_mono_48000_128k.mp3` | `ffmpeg -f lavfi -i sine=frequency=440:sample_rate=48000:duration=0.5 -ac 1 -c:a libmp3lame -b:a 128k -write_xing 0 -id3v2_version 0 mp3_mono_48000_128k.mp3` |
| `mp3_mono_32000_128k.mp3` | `ffmpeg -f lavfi -i sine=frequency=440:sample_rate=32000:duration=0.5 -ac 1 -c:a libmp3lame -b:a 128k -write_xing 0 -id3v2_version 0 mp3_mono_32000_128k.mp3` |
| `mp3_mono_24000_64k.mp3` | `ffmpeg -f lavfi -i sine=frequency=440:sample_rate=24000:duration=0.5 -ac 1 -c:a libmp3lame -b:a 64k -write_xing 0 -id3v2_version 0 mp3_mono_24000_64k.mp3` |
| `mp3_mono_16000_64k.mp3` | `ffmpeg -f lavfi -i sine=frequency=440:sample_rate=16000:duration=0.5 -ac 1 -c:a libmp3lame -b:a 64k -write_xing 0 -id3v2_version 0 mp3_mono_16000_64k.mp3` |
| `mp3_mono_8000_32k.mp3` | `ffmpeg -f lavfi -i sine=frequency=440:sample_rate=8000:duration=0.5 -ac 1 -c:a libmp3lame -b:a 32k -write_xing 0 -id3v2_version 0 mp3_mono_8000_32k.mp3` |
| `mp3_stereo_22050_96k.mp3` | `ffmpeg -f lavfi -i sine=frequency=440:sample_rate=22050:duration=0.5 -f lavfi -i sine=frequency=660:sample_rate=22050:duration=0.5 -filter_complex [0:a][1:a]join=inputs=2:channel_layout=stereo[a] -map [a] -c:a libmp3lame -b:a 96k -write_xing 0 -id3v2_version 0 mp3_stereo_22050_96k.mp3` |
| `mp3_mono_44100_2s.mp3` | `ffmpeg -f lavfi -i sine=frequency=440:sample_rate=44100:duration=2.0 -ac 1 -c:a libmp3lame -b:a 128k -write_xing 0 -id3v2_version 0 mp3_mono_44100_2s.mp3` |
| `wma_mono_44100.wma` | `ffmpeg -i tone_mono_44100.wav -c:a wmav2 -b:a 128k wma_mono_44100.wma` |
| `wma_stereo_44100.wma` | `ffmpeg -i tone_stereo_44100.wav -c:a wmav2 -b:a 192k wma_stereo_44100.wma` |
| `wma_mono_22050.wma` | `ffmpeg -i tone_mono_22050.wav -c:a wmav2 -b:a 64k wma_mono_22050.wma` |
| `wma_v1_mono_44100.wma` | `ffmpeg -i tone_mono_44100.wav -c:a wmav1 -b:a 128k wma_v1_mono_44100.wma` |
| `wmv_64x48_15fps_silent.wmv` | `ffmpeg -f lavfi -i testsrc2=size=64x48:rate=15:duration=1 -c:v wmv2 -b:v 200k -an wmv_64x48_15fps_silent.wmv` |
| `wmv_320x240_30fps_stereo.wmv` | `ffmpeg -f lavfi -i testsrc2=size=320x240:rate=30:duration=1 -f lavfi -i sine=frequency=440:sample_rate=44100:duration=1 -c:v wmv2 -b:v 300k -c:a wmav2 -b:a 128k -ac 2 wmv_320x240_30fps_stereo.wmv` |
| `wmv_v1_64x48.wmv` | `ffmpeg -f lavfi -i testsrc2=size=64x48:rate=10:duration=1 -c:v wmv1 -b:v 200k -an wmv_v1_64x48.wmv` |
| `empty.mp3` | zero bytes |
| `empty.wma` | zero bytes |
| `empty.wmv` | zero bytes |
| `truncated.mp3` | first 400 bytes of `mp3_mono_44100_128k.mp3` |
| `truncated.wma` | first 400 bytes of `wma_mono_44100.wma` |
| `truncated.wmv` | first 600 bytes of `wmv_64x48_15fps_silent.wmv` |
| `actually_wav.mp3` | `tone_mono_44100.wav` renamed: bytes that contradict the extension |
| `actually_mp3.wav` | `mp3_mono_44100_128k.mp3` renamed |
| `garbage.mp3` | seventeen bytes of text |
