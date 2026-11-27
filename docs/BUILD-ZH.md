# 构建与测试

## 构建应用

使用支持 devkitPro 的 Bash 环境；工程路径避免空格。需要 devkitARM、libctru、citro2d、citro3d、zlib、FFmpeg 6.1.2，生成 CIA 还需 bannertool 与 makerom。先自行准备工具链；本包不会安装依赖。

```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITARM="$DEVKITPRO/devkitARM"
export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH"
bash build-ffmpeg-3ds.sh
make -j2
make cia
```

FFmpeg 脚本下载并编译 6.1.2（含 MJPEG 编码器），写入工具链 portlibs，末尾使用 sudo make install；运行前核对自己的安装路径及权限。zlib 必须另由工具链提供。输出为根目录 `3danmu.3dsx`、`3danmu.cia`。具体环境说明见 `../DEVELOPING.md`。请在自己的工具链环境中执行构建并验证产物。

## 无设备主机回归

需要 Bash、C 编译器和 Python 3。在仓库根目录运行：

```bash
CC=gcc bash tools/test-host.sh
```

脚本沿用既有回归命令，并包含 v1.3.9 新增用例，外置测试已收入 `test/extra/`，输出写入 `build/host-tests/`。不需要登录、网络或 3DS。它验证模拟输入与核心逻辑，不能替代真机、MVD、音频、相册与在线接口验收。
