# Bread of the Wild · Content 整理与维护指南

本次在原路径完成本地整理，保留地图路径、界面布局和原存档。现已上传至 GitHub 私有仓库 djwcb2333/GDATtest；尚未邀请队员或进行异机同步验收。版本命名规则见同目录《版本管理与命名指南》。

## 工程入口和本地版本管理

日常开发打开 `E:/Unreal Project/GDATtest/GDATtest.uproject`。本次使用过的 OrgQA 是隔离验证副本；正式验证后按用户要求删除，日常只打开上面的原工程。

GitHub Desktop 左上角 **Current repository（当前仓库）** 选择 GDATtest。通过 **Repository → Show in Explorer（在资源管理器中显示）** 确认根目录含 `.uproject`、`Source`、`Content`、`Config`。仓库应覆盖整个工程。

**Changes（更改）** 显示尚未提交的修改。先在 UE 保存，关闭仍可能修改文件的操作，再填写左下角 **Summary（摘要）**，例如 `feat(gameplay): add berry resource node`，点击 **Commit to main** 建立本地记录。需要上传时点击 Push origin；完成后恢复 Fetch origin。仓库已发布，不必再次 Publish repository。

## 分类方式

| Content/DoughWorld 下的目录 | 放什么 |
|---|---|
| Core | 游戏模式、控制器、全局数据、前端基础蓝图 |
| Characters | 主角、敌人、角色模型、动画和相关蓝图 |
| Gameplay | 战斗、资源、游戏机制资产 |
| UI | HUD、菜单、控件、界面材质、字体、光标 |
| Audio | Music 音乐、SFX 音效、Voices 拟声、Mixing 混音配置 |
| Cinematics | 过场、字幕、对话数据和相关界面 |
| Environment | 项目自用环境材质 |
| VFX | 项目自用视觉效果 |
| Fab | 外部素材包；各包保留自己的原有内部分类 |
| Maps | 地图仍在原来的具体路径，连同构建数据及外部 Actor 数据保留 |

`Fab` 的作用是区分素材来源。包内的 Meshes、Materials、Textures 仍有必要：前者回答“来自哪个包”，后者回答“是什么资源”。不要把不同包同名材质合并。用户已经放入 Fab 的 Fantastic_Village_Pack 保持现状。

`Stylized_Scene` 与 `Stylized_Scene_MapVariant` 分别保留两个现有来源；名称相近不代表内容相同。Programming、模板、历史原型仍保留，不能仅凭名称判断可删除。

## 为什么部分旧目录还可能可见

旧地图路径关系到存档。本轮优先保持地图兼容，所以素材包的示例地图可能仍位于 Maps 内。UE 的 **Redirector（重定向器）** 是旧路径指向新路径的兼容资源，不是重复模型。不要在资源管理器里删除它。

内容浏览器通过 **Ctrl+Space（内容浏览器抽屉）** 打开。在筛选器 **Other Filters → Show Redirectors（显示重定向器）** 检查旧路径兼容对象。是否清理必须结合引用检查、源码路径和运行验证决定，不能一看到旧文件夹就删除。

## 后续添加和移动资源

1. 新资源直接放到合适目录，取稳定英文名；资产前缀使用项目现有规则，如 BP_、WBP_、M_、MI_、T_、SM_。
2. 移动既有资源前保存所有修改，在 GitHub Desktop 提交一个回退点；多人协作时先约定唯一写入者。
3. 通过 UE 内容浏览器移动，选择 **Move Here（移动到此处）**。不要在 Windows 资源管理器拖动 `.uasset` 或 `.umap`。
4. 右键资源 → **Reference Viewer（引用查看器）** 检查使用关系。源码和配置中的字符串路径也必须检查，它们未必会被自动更新。
5. 每批移动后保存，关闭并重开工程，打开相关地图、蓝图，实际游玩受影响功能。
6. 对照 GitHub Desktop 的 Changes，确认改动范围符合预期，再提交。本地提交不是上传。

## 删除资源前

删除模型之前先处理关卡实例、植被类型、蓝图和材质等引用。弹出引用警告时不要直接选择 **Force Delete（强制删除）**。不确定用途的资源保留，先记录待整理。

本次删除的指定树包是 `Low_Poly_Tree_with_twisting_branches-053c6fcb`。同时从 SceneBuildTest 移除其 116 个植被实例；该地图的 23 个 Actor 和其他植被实例保留，树包引用已清零。

## 出现异常时

暂停保存并记下当前地图、报错和最后一次正常提交。不要使用 Discard all changes 清掉工作。由负责执行的人对照备份和 Git 差异进行针对性恢复；原存档不属于普通 Git 回退范围，单独备份保护。


## 本次验收与回退点

- 本地整理验收版本：`v0.1.0-alpha.2`。这里只表示本轮本机验收完成，不代表发行版本。
- 整理前回退点：`6f18f30`，包含用户已经做过的村庄迁移。
- 分类与树包删除提交：`0d303cb`；村庄纹理迁移收尾提交：`f4c9452`。
- 3,135 个分类目标资源冷启动加载通过，85 个迁移蓝图编译通过；15 个 WBP 的控件、布局和导出样式属性对照一致。
- 正式工程 C++ 编译通过；副本 20 项自动回归、正式工程独立运行 26 项流程检查通过。
- 原有 9 份存档保持不变。旧档备份可读取、地图存在；并未逐一完整试玩所有历史存档。
- 素材包原有 25 条缺失依赖仍存在，本次没有新增缺失依赖。未进行发行打包或其他电脑环境验收。
- 重开编辑器仍有整理前就存在的 GameFeatureData 资产管理器配置提示；本轮未改动其配置，也未将该提示误写为已解决。
- 验证副本 OrgQA 及临时英文目录联接已删除；正式 Git 历史、原存档备份和验收报告保留。

如需回到整理前，先停止编辑并保护后续新增工作，再由维护者按上述提交比较和恢复。回退不等于删除整个仓库；Git 中的历史和 LFS 对象必须保留。个人存档在 Saved/SaveGames，不随 Git 自动回退。

## 本地检查顺序

1. 双击原路径 GDATtest.uproject，等待资源和着色器准备完毕。
2. Ctrl+Space 打开内容浏览器；进入 Content → DoughWorld，即可看到新的分类。Fab 中按素材包查找，UI 中按用途查找。
3. 打开原地图。地图路径没有统一搬到新位置，已有地图入口继续使用。
4. 点击顶栏绿色 Play（播放）验证当前地图；Esc 停止后再保存。
5. GitHub Desktop → Changes 核对自己的新修改，填写 Summary 后 Commit。文件数量很多时先核对范围，不要为了清空列表点 Discard。

详细验收报告和备份位于工作区 UE_Whitebox_Delivery/ContentOrganization_20260919；它不是日常游戏工程。本 HTML 版内嵌实拍图，可离线打开。
