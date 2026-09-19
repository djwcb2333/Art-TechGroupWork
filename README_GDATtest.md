# Bread of the Wild / Dough World — GDATtest

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
