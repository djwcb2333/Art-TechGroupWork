# 风格化资产检查与替换

## 发现的问题

项目为 `E:/Unreal Project/GDATtest`。资产同时存在于 `/Game/Stylized_Scene` 和 `/Game/Maps/Stylized_Scene`。目录重复本身不会直接导致渲染错误，但 Maps 中的网格材质仍指向前一个目录；读取时多处材质实例的 parent 和贴图参数为 None，主材质的纹理节点也为空。

实际编译错误包括 `Found NULL, requires Texture2D` 和 `Missing input Virtual Texture`。因此确实存在导入后的引用问题，不能仅凭内容浏览器中有文件就认为导入完整。现有证据不能确定最初是哪一步导入/移动操作造成了断引用。

## 已完成的修复

修复范围是 `Content/Maps/Stylized_Scene`。保留 `Content/Stylized_Scene` 原副本，没有删除重复目录。

- 恢复树、树皮、花、草、地形材质的默认贴图 / RVT 引用；按已有依赖信息重连，保留原材质图结构。
- 恢复 19 个材质实例的父材质与相关覆盖贴图。
- 将 13 个静态网格的材质槽指向 Maps 内已修复的材质实例。
- 6 个主材质重新编译通过；13 个网格均有有效包围盒和非零三角形。
- 树冠、树干、花朵缩略图已实际查看，恢复了有色叶片、树皮与透明裁切花瓣。

树的叶片覆盖选择依据原依赖记录；当某个实例依赖两张叶片图时，保留基础 Leaf_05 作为主材质默认，并使用该实例的另一张叶片图作为覆盖。

## 场景替换

新关卡：`/Game/Maps/CrumbFrontier/Levels/L_CrumbFrontier_ArtPass`。

原白盒：`/Game/Maps/CrumbFrontier/Levels/L_CrumbFrontier_Whitebox`，保持原样。

新蓝图：`/Game/Maps/CrumbFrontier/Art/Blueprints/BP_Art_EnvironmentTree`。`Tree_02` 是树冠，`Trunk_01` 是树干，两者组合成整树，使用统一 0.4 部件缩放并校正底部高度。原实例的位置、旋转、缩放、标签均保留。树冠和树干显示网格不承担碰撞；新增不可见的简化圆柱树干碰撞，避免细枝和叶片阻挡通行。

共替换 12 个 `Dress_4_SporeTree_*` 环境树占位。这里按“环境树”类别匹配，原孢子主题名称保留用于对照；新模型是普通风格化树，并非已经完成世界观专用的孢子树美术。替换实例额外带 `ArtReplacement=EnvironmentTree` 和 `OriginalPlaceholder=BP_PH_SporeTree` 标签。

## 没有强行替换的内容

这套包主要提供树、草、花，没有对应的麦穗、芦苇、酵母泡簇、角色、资源、村屋或桥。上述内容保留原占位。没有将花当作酵母泡、草当作麦穗，也没有根据 A/B/C 等资源名猜测其真实外观。

草材质采用 RVT 地表采样。虽然已经修复引用并编译通过，但白盒使用静态台地，没有布置该资产包要求的 RVT 地表写入与体积；因此本轮不把草材质直接用于白盒地表。资产包自带示例关卡、烘焙数据与风粒子未做完整运行验收，不能据此次修复断言整个示例工程已完全可用。原目录中的另一份损坏材质仍可能产生旧错误，应使用 Maps 内修复后的版本。

## 打开与对照

在内容浏览器进入 `Maps/CrumbFrontier/Levels`，打开 `L_CrumbFrontier_ArtPass`。在大纲搜索 `_Art`，选中树后按 F 聚焦。若要对照原始地图，打开 `L_CrumbFrontier_Whitebox`。

详细修复前节点记录、网格检测数据和每个替换实例的变换见 `stylized_import_audit.json`。不要直接从文件管理器删除另一套目录；如后续需要清理，应先在 UE 内检查引用与重定向器。

