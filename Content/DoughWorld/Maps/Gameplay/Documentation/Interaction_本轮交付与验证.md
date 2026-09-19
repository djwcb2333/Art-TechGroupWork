# 字体、语言、交互提示与橙色变身

本轮已更新到 E:/Unreal Project/GDATtest，使用地图 Content/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_GameplayPrototype。

- 菜单、HUD、背包、制作、资源提示统一中英手绘风格字体；设置中“语言 / Language”即时切换并单独保存。
- 水、酵母、面粉、面团使用物体上方弹性提示，F 键帽和真实采集进度；样式、字体、键盘音、动效参数可修改。
- 酵母形态逐渐变橙并保持约 1.36446 倍体型，动画切到待机不会回缩或重复放大；退出默认 0.45 秒恢复。
- 提供 28 个 UI 与采集音效事件槽；UI 点击与提示出现已分配原创键盘声，其他事件槽为空时静音，便于后续填入对应素材。BGM、变身声音接口保留。

## 在哪里修改

- 设置入口：标题页 → 设置；或游戏中 Esc → 设置 → 语言 / Language。
- 字体：BP_DWGameplayHUD → Game UI Font；默认 F_DWHandDrawn。
- 物体提示：UI/Interaction/DA_DWInteractionPromptStyle；布局使用真实 WBP_DWInteractionPrompt Designer。
- 变身体型和颜色：BP_DWTopDownCharacter → 类默认值 → Transformation → Visual。
- UI、面板、制作、采集声音：Data/DA_DoughWorldGameplay → Audio 各分类。
- 可编辑物品和配方英文名：DT_DWItems、DT_DWRecipes → EnglishDisplayName。

## 本轮实际验证

- 最终 C++ 编译成功；已有 DoughWorld 自动回归测试 13 项通过。
- 交互提示 PIE 检查 48 项通过：焦点、过冲、F 采集进度、退出、暂停、切语言不重播等。
- 音效与变身 PIE 检查 77 项通过：空槽静音、28 槽可播放、循环音松手和暂停清理、动画结束保持橙色放大、退出恢复与逐帧尺寸衔接。
- 完整关闭再打开编辑器后，语言从独立偏好文件恢复为测试时选定的 English。
- 检查结束已恢复为简体中文；中英文 HUD 均有实际游戏截图，右上角面板已调整以容纳英文。
- 原存档 3 的 SHA-256 未变；测试临时存档已按专用名称核对后清理。
- 字体原始字节已嵌入 FontFace；字体许可文件随交付保留。未在本轮打包 EXE 或执行完整 Cook。

详细配置见《交互提示与语言设置使用指南.md》。声音自动验证只确认实际播放调用和循环生命周期，不等于人工听感验收。
