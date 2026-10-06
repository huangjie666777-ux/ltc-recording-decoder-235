# ltc_scan235

离线 LTC（Linear Timecode）录音解析工具。从 48 kHz / 16-bit PCM 单声道或
立体声 WAV 录音的时码声道中，自行从采样恢复双相标记编码（biphase-mark）
位流与 SMPTE 80 位帧，输出 JSON 格式的逐帧时间码与连续段摘要。无界面、
不读视频、不播放音频、不提供网络服务，原录音不被修改。

## 构建

需要 C++17 编译器（g++）与 GNU Make。依赖 dr_wav 0.14.2 与
nlohmann/json 3.11.3，已内置于 third_party/。

    make            # 生成 build/ltc_scan235 与 build/gen_wav
    make test       # 构建并运行测试套件（需要 python3）

## 调用

    build/ltc_scan235 <input.wav> [--channel N|auto] [--chunk N]
                      [--summary] [--output file.json]

- --channel N：时码声道（0 或 1）。auto（默认）取单声道唯一声道或
  立体声的第一个声道；其余声道不解码。
- --chunk N：每次读取的采样帧数（默认 4096）。分块读取跨块保留状态，
  改变块长不会改变有效帧及位置（测试 10 验证）。
- --summary：只输出段摘要，不逐帧展开。
- --output：JSON 写入文件而非 stdout。

## 输出

JSON 顶层含文件信息（采样率、声道、所选时码声道、25fps 非丢帧）与
segments 数组。每段含首末时码、帧数、起止采样位置（从 0 起的半开区间）
与秒数、中断原因（signal_lost / bad_frame / timecode_jump /
position_gap / end_of_audio），以及逐帧记录：时码、80 位内容
（20 个十六进制字符，bit 0 为最低位）、采样半开区间与秒数。

## 支持范围

- 仅 48 kHz、16-bit PCM、单声道或立体声 WAV。
- 仅 25 fps 正向非丢帧 LTC；丢帧标志置位或 BCD/范围非法
  （小时 0-23、分秒 0-59、帧 0-24）的帧不交付。
- 支持极性反转、任意前置静默、从帧中途开始的录音；容忍恒定播放速度
  偏差约 ±2%（按边沿间隔跟踪位周期，不按固定样本数切位）。
- 同步字失锁后继续搜索；首尾残帧不输出；静默断码、坏帧或时码跳变
  结束当前段，重新捕获后另起一段；不插值补造帧。连续段仅在音轨位置
  相邻且时码加一时延续，允许 23:59:59:24 → 00:00:00:00 午夜回绕。

## 源码结构

- src/audio.{h,cpp}：WAV 分块读取与声道选择（dr_wav）。
- src/biphase.{h,cpp}：过零边沿检测、位周期跟踪、双相标记位恢复。
- src/ltc_frame.{h,cpp}：同步字对齐、80 位帧组装与 BCD 校验。
- src/segments.{h,cpp}：连续段整理与中断原因判定。
- src/main.cpp：命令行与 JSON 输出。
- tools/gen_wav.cpp：可复现测试信号生成器（静默、极性、变速、
  断码、跳变、坏帧、帧中途起始等）。
- tests/verify.py、tests/run_tests.sh：测试套件。

## 可复现示例

    # 生成立体声示例：时码在声道 1，0.5 s 前置静默，+1.5% 速度
    build/gen_wav --out example.wav --start 01:00:00:00 --frames 250 \
        --channels 2 --tc-channel 1 --silence 0.5 --speed 1.015
    build/ltc_scan235 example.wav --channel 1

    # 含断码与跳变的示例
    build/gen_wav --out example2.wav --frames 400 --gap-at 150 \
        --gap-len 0.25 --jump-at 300 --jump-by 250
    build/ltc_scan235 example2.wav --summary
