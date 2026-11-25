# 3Danmu Kina

Nintendo 3DS 非官方视频客户端，基于 [OakRoller/3Danmu](https://github.com/OakRoller/3Danmu) 的独立改版。

当前功能版本：**1.3.8-kina-favtouch**。本仓库快照按 2026-11-25 整理。

新增功能包括稍后再看管理、UP 搜索与个人中心、多收藏夹、点赞投币收藏、5 秒跳转、倍速、播放策略与队列连播、3DS 相册截图，以及输入法与导航改进。

- [完整功能对比与验证边界](docs/FEATURES-ZH.md)
- [编译与主机测试](docs/BUILD-ZH.md)
- [GitHub 上传说明](docs/GITHUB-UPLOAD-ZH.md)
- [本次验证结果](docs/VERIFICATION.md)
- [原版及既有开发使用说明](docs/README-original-and-development.md)：保留原文作历史参考，其中旧版操作描述以本页及功能对比为准。
- [开发细节](DEVELOPING.md)、[测试指南](TESTING.md)、[第三方声明](THIRD-PARTY-NOTICES.md)、[项目许可](LICENSE)。

## 主要操作

列表页 X 管理稍后再看，Y 搜索。播放页 X 打开互动，Y 截图；L/R 或左右跳转 5 秒，A 长按临时 2 倍速。收藏夹选择页上/L、下/R 翻页，触屏勾选，X 两次确认提交，B 返回。

## 应用身份

名称 `3Danmu Kina`；Title ID `000400000F8DA100`；数据目录 `sdmc:/3ds/3danmu-kina/`。原版 Title ID `000400000B1B1100`。同 ID Kina 包覆盖安装；与原版分开。CIA 安装元数据版本仍为 1024（1.0.0），功能版本在设置页查看。

本包为源码与说明，不附 CIA/3DSX、账号数据、系统固件或编译工具链。保留 `romfs/font.bcfnt`、`romfs/pinyin.dic`、图标与横幅等必要构建输入。

## 来源与许可

原版作者归属保留于 LICENSE 和原说明。上游基准提交 `ebbe757605135bace131651cc563c8a47cdf7a6a`，原版 v1.2.0。改版不是上游官方发布。

项目代码沿用原仓库 MIT 许可，第三方字体、词库与组件分别按 `licenses/` 中的许可分发。发布链接 FFmpeg 的二进制时还需按第三方许可准备对应源码及可重新链接材料；本源码 ZIP 不替代完整的二进制发布合规检查。

本项目与哔哩哔哩、任天堂无关联，沿用原项目的非官方用途说明与访问限制。
