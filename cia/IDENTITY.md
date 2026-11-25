# 3Danmu Kina 固定安装身份

用户明确要求：改版与原版并存；后续改版覆盖更新同一个改版。

| 项目 | 改版固定值 | 原版 |
|---|---|---|
| Title ID | `000400000F8DA100` | `000400000B1B1100` |
| RSF UniqueId | `0xF8DA1` | `0xB1B11` |
| HOME菜单名称 | `3Danmu Kina` | `3Danmu` |
| NCCH Title | `3DanKina` | `3Danmu` |
| ProductCode | `CTR-P-3DKN` | `CTR-P-3DAN` |
| SD数据目录 | `sdmc:/3ds/3danmu-kina/` | `sdmc:/3ds/3danmu/` |

今后更改功能或版本时，保留以上改版值。安装同Title ID的CIA更新改版；不要每次生成新ID，不要恢复原版ID。第一次改版登录需用户单独扫码；没有原版登录/设置迁移。

数据目录常量在 source/app_identity.h；设置、登录存储、封面与日志均引用它。新增缓存/截图等写入需继续遵守隔离规则（用户指定相册/Luma截图位置的功能另按该功能约定）。

发布前核对实际CIA的ticket、TMD、NCCH和exheader中的ID；核对菜单SMDH名称及源码数据路径。只改文件名不改变安装身份。设备上是否还有其他同ID自制软件无法离线确认；至少已验证本项目原版ID与改版不同。
