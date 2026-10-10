# Bread of the Wild / Dough World — GDATtest

## 当前版本：v0.3.0-alpha.2 · 编译与稳定性修复版

2026-10-10。本次为 v0.3.0-alpha.1 的错误修复，没有新增功能。包含 Unity 合并编译 C4459 重名修复、读档/转场时的目标槽保护、关闭动画切图后恢复保存、合成提示与现有变身开关一致，以及演示文字 float 警告清理。详细修复范围、已有验收与组员同步步骤见 [版本记录](版本记录.html) / [Markdown](版本记录.md)。

组员先保存并关闭 UE，再 GitHub Desktop → Fetch origin → Pull origin；生成项目文件并构建 Development Editor / Win64 的 GDATtestEditor 后重新打开。旧 v0.3.0-alpha.1 标签继续固定指向旧快照，不能代替新修复源码。以下 2026-10-09 条目说明修复过程，当前发布范围以本节和版本记录为准。

## 2026-10-09 项目检查后续：保存保护与合成提示

本轮保留上方/下方记录的Unity重名修复，另外修复了保存和提示问题：

- 开始读档后，活动槽位已变为目标槽，而旧关卡玩家仍可能存在。现在 `SaveCurrentGame` 在读档、角色恢复或转场结束前返回false并保留目标存档，完成后正常保存。
- 关闭加载动画时，`TravelToGameplayMap` 也正确结束内部旅行状态，避免后续保存一直被阻止。
- 合成和变身通知遵循 `DA_DoughWorldGameplay → Crafting → Require Transformation For Crafting` 开关与配方要求。默认关闭时，不再提示必须先变身。
- 演示文字控件的颜色常量明确使用float，消除该处C4305警告，颜色值不变。

蓝图使用说明：调用 `Save Current Game` 后连接 **Branch / 分支** 检查返回值。true才显示保存成功；false时从 `Last Save Error` 读取原因。加载或切图开始后等待完成再保存。现有保存UI已经检查返回值，无须重建控件。合成开关入口仍为内容抽屉 `DoughWorld/Core/Data/DA_DoughWorldGameplay`，不需重建配方。

本机检查：Editor和Game Development强制Unity构建成功；全部32项DoughWorld自动化测试通过（3项带预期非法输入/测试世界/字体回退警告）；隔离独立运行200/200；最终Windows Development打包成功，包内201/201，加小地图回归64/64。新增检查确认了目标槽字节不变，以及正常加载和关闭动画切图后恢复保存。本轮未做Shipping、异机或所有地图的逐项人工试玩。

UE5.8实验工具插件在UnrealEditor.exe -game启动时仍有Python初始化错误；普通编辑器命令行测试无此错误，最终打包程序也无此错误。没有修改引擎安装、禁用依赖插件或修改驱动。

2026-10-09 检查结束时累计待提交为6个cpp和本README（共7个文件）；这些修复现纳入 v0.3.0-alpha.2。组员仍需拉取修复提交后生成Development Editor/Win64，详见下方同步说明。不要用旧阶段安装脚本覆盖当前源码。现有资产、配置和正式存档未变。


## 2026-10-09 修复：同步后C4459编译失败、小地图未显示

本次为v0.3.0-alpha.1后的源码修复，不移动资源、不改玩法。`GameplayUXRuntimeTests.cpp`中的五个局部Panel变量、一个Names数组，与其他cpp中的匿名全局变量同名。Unity合并编译把它们放进同一编译单元后触发C4459，无法生成新的DLL。不同电脑或干净同步后的文件分组可能不同，之前本机增量编译成功不足以排除此问题。

已把测试变量、UI颜色常量和音量分类数组改成各自专用名称。只更改C++标识符，字符串、颜色值、设置键、资产路径、存档和游戏逻辑保持。

### 作者上传步骤

此处原为 Unity 首轮的 3cpp+README 上传待办，已合并后续保存/提示修复，纳入 v0.3.0-alpha.2。查看本版标签和根级版本记录；后续修复另建新提交/标签，不移动旧标签。

### 组员同步与重新编译

1. 先在UE保存自己的工作并关闭编辑器。若GitHub Desktop显示自己的未提交修改，先妥善保留，不要点击Discard all changes。
2. 在GitHub Desktop选择本项目，点 **Fetch origin → Pull origin**，等待源码和Git LFS资产下载完成。确认拉到了包含本次修复的新提交；旧v0.3.0-alpha.1标签仍指向原版本，不会自动改变。
3. 右键本机的 `GDATtest.uproject` → **显示更多选项 → Generate Visual Studio project files**，然后打开同目录 `GDATtest.sln`。生成项目文件本身不等于编译。
4. Visual Studio顶部选择 **Development Editor / Win64**，在解决方案资源管理器中右键 **GDATtest** 游戏项目 → **Build / 生成**（目标为GDATtestEditor）。不要只编译Development游戏目标。先关闭UE再构建，避免旧DLL仍被占用。
5. 等待输出显示生成成功、没有error。确认 `Binaries/Win64/UnrealEditor-GDATtest.dll` 的修改时间已更新，再双击 `.uproject`。无需删除Content、Saved或任何个人存档。
6. 打开现有Map1并Play，检查右下角圆形小地图；按住中键转动时地图同步旋转；打开设置应有图像、声音、按键、控制器、常规五个分类。

### 维护者复现同类构建问题

使用本机实际UE安装路径运行下面命令；替换两处示例路径，不要照搬作者磁盘位置：

```powershell
& '你的UE安装目录/Engine/Build/BatchFiles/Build.bat' GDATtestEditor Win64 Development '-Project=你的项目目录/GDATtest.uproject' -WaitMutex -NoHotReloadFromIDE -ForceUnity -DisableAdaptiveUnity
```

`-ForceUnity`强制合并编译，`-DisableAdaptiveUnity`防止已修改文件被单独编译而掩盖冲突。这是验证用参数，没有永久关闭Unity或屏蔽编译警告。

本机UE5.8.2：修复前在合并构建中准确复现6个C4459；修复后Editor和Game Development合并构建均成功，加载新DLL的隔离独立运行184/184通过。DWTextRevealDemo已有的浮点截断警告仍在，与本次失败无关。组员电脑重编译仍需由组员验证；本轮未重新打包、未做PIE/Shipping/全玩法测试。详细记录保存在作者工作区 `UE_Whitebox_Delivery/GameplayUX_20261004/UnityFix_20261009_*`。


## 上一版本：v0.3.0-alpha.1 · 小地图与交互优化版本

2026-10-05。完整更新内容、玩家操作、编辑入口、默认参数及验证边界见 [版本记录](版本记录.html)（[Markdown](版本记录.md)）。本版包含小地图与镜头同步、五书签设置/主音量、中键拖拽与滚轮、背包分堆/合并/丢弃、合成变身开关、对白左键下一句，以及存档和打包修复。

当前私有仓库为 [djwcb2333/Art-TechGroupWork](https://github.com/djwcb2333/Art-TechGroupWork)，Clone URL：`https://github.com/djwcb2333/Art-TechGroupWork.git`。工程路径与入口继续沿用 GDATtest；后面的旧版本章节属于历史记录。

打包游戏进度使用当前 Windows 用户的“文档/DoughWorld”，可在 Project Settings → Project → DoughWorld Saves → Documents Folder Name 修改下一次构建采用的名称。编辑器 PIE 仍用工程 Saved/SaveGames。个人存档和设置不上传。

这是完整的 Unreal C++ 可编辑工程。仓库根目录是包含 `GDATtest.uproject` 的目录。

版本号、Alpha/Beta/RC、Git 分支、提交与标签的命名规则，见同目录的 [版本管理与命名指南](版本管理与命名指南.html)（[可编辑 Markdown](版本管理与命名指南.md)）。

## 文件与环境

- 引擎：建立仓库时本机为 **Unreal Engine 5.8.2**，`.uproject` 的 EngineAssociation 为 `5.8`。
- C++ 模块：`Source/GDATtest` 和 `Source/Programming/ArtTechCollaboration`。两个模块均保留。
- 必须一起获取：`.uproject`、`Config`、`Source`、完整 `Content`、`.vsconfig`、Git LFS 文件；将来加入的项目插件和 Build 资源也应纳入。
- 不包含可再生成的项目 `Binaries`、`Intermediate`、`DerivedDataCache`、Visual Studio 缓存、生成的解决方案或个人 `Saved`。首次打开需要编译。
- 不分发个人存档。需要存档备份时，单独备份 `Saved/SaveGames`。

## 同学第一次获取

1. 安装 GitHub Desktop、相同版本 Unreal Engine，以及 Visual Studio 的 **Game development with C++ / 使用 C++ 的游戏开发** 工作负载和 Windows SDK。项目 `.vsconfig` 记录原机组件，本机包含 MSVC 14.44 和 Windows SDK 22621；不要直接照搬另一台电脑的绝对路径。
2. 仓库发布后，接受私有仓库邀请。在 GitHub Desktop 选择 **File → Clone repository… → URL**，填团队提供的真实仓库地址，Local path 选择自己电脑上新的空目录，点击 **Clone**。不要选现有工程目录。
3. 等待克隆和 LFS 下载完成。在 **Repository → Open in Command Prompt** 打开的窗口检查 `git lfs pull`，然后 `git lfs ls-files`。大资产必须是实际文件，不能只是约 130 字节、首行为 `version https://git-lfs.github.com/spec/v1` 的指针文本。
4. 确认下述插件在自己的引擎中可用。右键 `GDATtest.uproject`，必要时选择 Windows 的 **显示更多选项**，再选 **Generate Visual Studio project files**。菜单不存在时先检查 UE 文件关联和 C++ 工具安装。
5. 打开生成的 `GDATtest.sln`。工具栏选 **Development Editor / Win64**，在解决方案中构建游戏项目（对应目标 `GDATtestEditor`）。成功后双击 `.uproject`。
6. 默认入口为 `/Game/DoughWorld/Maps/Gameplay/Frontend/Levels/L_DWMainMenu_Showcase`。至少检查标题、进入游戏、移动、UI、字幕与存档；在自己的测试存档中验证，不覆盖别人的存档。

## 引擎插件依赖

`.uproject` 当前启用 `ModelingToolsEditorMode`、`StateTree`、`GameplayStateTree`、`ModelContextProtocol`、`MCPClientToolset`、`AllToolsets`。本机后三者位于 UE 5.8 的 `Engine/Plugins/Experimental`，而不在项目里。

其中 `ModelContextProtocol` 与 `AllToolsets` 描述文件标记 `NoRedist=true`。这个仓库不会把引擎插件复制进项目；接收方应通过自己的合法引擎安装取得匹配插件。若接收方环境没有这些插件，需要团队另外决定依赖调整，本次版本管理不会修改 `.uproject` 或禁用插件。

## 日常协作

1. UE 中停止运行、保存改动并关闭编辑器，再进行拉取或分支切换。
2. 在 GitHub Desktop 确认仓库根目录；它必须包含 Source、Config 和 `.uproject`。不要继续使用旧 Content-only 仓库。
3. 在 **Changes** 检查改动，输入 **Summary**（本次改了什么），点击 **Commit … to main**。Commit 仅保存本地历史。
4. 只有团队决定发布后才使用 **Publish repository / Push origin**。**Fetch origin** 检查远程，**Pull origin** 把远程内容带回本地。
5. C++ 更新后完整编译再打开 UE。蓝图和地图难以自动合并，同一资产提前约定唯一修改者；GitHub Desktop 不会自动帮团队锁住资产。
6. Git 的回退、丢弃改动、拉取和切换分支会改工作文件，先保存与核对。不要对共享工程随意使用 **Discard changes**，也不要执行 `git reset --hard` 或 `git clean`。

## 当前交付边界

本地建立 Git 不等于已经上传，也不等于其他电脑编译成功。以外部交付目录中的本次验收记录为准。商城/外部资产的授权不因进入仓库而改变；共享时应核对接收者与资源许可。

参考：[GitHub Desktop 添加本地仓库](https://docs.github.com/en/desktop/adding-and-cloning-repositories/adding-a-repository-from-your-local-computer-to-github-desktop)、[Git LFS 与 Desktop](https://docs.github.com/en/desktop/configuring-and-customizing-github-desktop/about-git-large-file-storage-and-github-desktop)、[克隆仓库](https://docs.github.com/en/desktop/adding-and-cloning-repositories/cloning-and-forking-repositories-from-github-desktop)。

## 2026-09-19 本地 Content 整理验收

本地版本 `v0.1.0-alpha.2`：分类与指定树包清理已完成，村庄素材迁移引用已收尾。操作和分类表见同目录 [Content 整理与维护指南](Content整理与维护指南.html)（[Markdown](Content整理与维护指南.md)）。日常仍打开原路径 GDATtest.uproject。

C++ 编译、3,135 个迁移资源冷启动加载、85 个蓝图编译、15 个 WBP 对照和 26 项独立运行检查通过；原有 9 份存档保留。25 条素材依赖缺失和 GameFeatureData 配置提示为原有问题，未声称已解决。发行打包和其他电脑构建未验证。OrgQA 验证副本已删除，正式 Git 回退历史保留；现已通过 GitHub Desktop 上传至私有仓库；远程地址见下方。

## GitHub 私有仓库（2026-09-19 已发布）

仓库：[djwcb2333/GDATtest](https://github.com/djwcb2333/GDATtest)。Clone URL：`https://github.com/djwcb2333/GDATtest.git`。

完整 main 分支及版本历史已上传，`v0.1.0-alpha.1` 和 `v0.1.0-alpha.2` 已在 Desktop 核对无待上传标记。本轮没有邀请队员；队员需要先获得私有仓库访问权限，然后在 GitHub Desktop → File → Clone repository… → URL 克隆到新的空目录。

日常先在 UE 保存，再到 Desktop → Changes 核对并 Commit，最后点 Push origin。Commit 只保存本地记录；推送完成后按钮恢复 Fetch origin。不要重复 Publish repository，也不要为了同步使用 Discard all changes。个人存档不上传。

首次推送包含约 5.7 GB 的 LFS 历史资源，耗时约 24 分钟。上传成功不是异机编译、运行或发行打包验收；接收方仍需按上文配置引擎和 C++ 环境。已发布的版本标签保持不动，后续版本创建新标签。

## 当前里程碑：期中验收版本（暂定）

版本 **v0.2.0-alpha.1**（2026-09-20）。演示地图为 L_DoughWorld_Map1，包含过场、方向路标、独立镜头晃动及本阶段音频/界面调整。完整范围、版本命名和验证边界见 [版本记录](版本记录.md)。现有启动菜单流程保持不变。