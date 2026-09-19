# Fantastic Village 美术替换接入说明

本说明针对2026-09-13批次。修改对象仍是 `/Game/Maps/DoughWorldDepth/Levels/L_DoughWorld_Depth`，没有新建替代地图。`BuildReport.json` 记录30份复制网格、60个新增实例、56个旧代理隐藏，构建记录已保存且errors为空。此处记录实际实施状态；运行证据和使用入口见[README.md](README.md)。

## 资源与场景组织

- 新网格：`/Game/Maps/DoughWorldDepth/Village/Meshes`。
- 关卡新增Actor标签：`FV__*`；附有 `VillagePass` 标签。
- 编辑器Actor文件夹：`Depth/Village/<module>`；旧隐藏几何放在 `Depth/Village/ReplacedProxyGeometry`。
- 网格仍引用 `/Game/Fantastic_Village_Pack/materials` 内原材质实例，共14种，其母材质和纹理也继续保留在原包目录。新副本位于Maps，但它们不是脱离原包即可迁移的独立资源包。
- `BuildReport.json` 的 `new_assets` 对应源/目标资源，`placements` 对应实际Actor、module、source_ref、transform、材质和碰撞，`replaced_visuals` 对应隐藏代理与碰撞保留状态。

迁移给程序时通过UE依赖迁移，连同原材质、母材质、材质函数和贴图一起处理。不要直接删除Fantastic_Village_Pack目录，也不要只发送30份SM。没有修改源包的材质或覆盖整个SoStylized地形体系，暂不使用RVT。

## 原图节点和接口

50个原始源锚点继续保留。[VillageVerification.json](VillageVerification.json)核对其XYZ相对本轮修改前最大差值为0cm；模块ID、资源/人物/机关源ID和原教程路线仍按之前定义使用。这里的“原位置保持”针对源节点，不表示所有装饰几何Z都不变。

| 原建筑/机关锚点 | 原图XY（m） | 当前用途 |
|---|---|---|
| `buildings/base_hut` | 221.7, 243.3 | 基地面团居所；周边增加低家具 |
| `buildings/workbench` | 221.7, 266.7 | 制作台；外观替换为table_02与木碗 |
| `buildings/storage_repair` | 196.7, 256.7 | 仓架、箱筐、修理设施 |
| `buildings/ruined_settlement` | 333.3, 166.7 | 原面团废墟内外增加废弃货物 |
| `mechanisms/mech_plate_1` | 393.3, 170.0 | 原桥与机关关联；仅升级桥面外观 |

Manifest的 `bottom_xyz_m` 使用原图平面坐标和米制底面Z；转换为UE世界厘米：`X=(x-250)*100`、`Y=(y-250)*100`、`Z=z*100`。Actor自身枢轴不一定在底面，因此接入时以实际Actor/component transform为准。

制作台的 `flour_bud` 和 `rolling_pin` 两个现有装饰件仅调整Z，贴合新桌面；它们不是50个源锚点。所有新增小道具都只是原区域的美术关联，不能把空source_ref的装饰解读成新增交互ID。

本轮没有修改GameMode、Character、Controller、跳跃/翻滚输入或传送代理代码。程序仍通过既有蓝图/C++接口接入；基础说明见上一级 `Integration.md`。`FV__`演员名用于识别场景摆件，不作为新的游戏机制协议。

## 真实表面贴合与最终坐标

最终 `BuildReport.json.support_refinements` 有11条：4个RackFlour、3个RackBreadBasket和HutBread按目标真实网格射线命中面加0.003m重新放置；3个StorageBox底面降低至6.39m，并按0.88/0.94等比缩小，以适配货架净空。这些修正针对装饰件的Z和尺寸，未移动原50个节点的XY。

新增 `FV__CartLoad0` 为第60个实例，复用 `SM_PROP_sack_01`，在 `(228.5,262.5)` 对SupplyCart车床进行射线后贴合。记录底面Z约7.02555m、无碰撞；该车载袋不增加装载/搬运机制。最终Manifest1110条（原1050+新增60），场景总数1171Actor，sack_01共5个实例。

程序修改摆件时采用最新 `placements` 的 `bottom_xyz_m` / `dimensions_m` 和Actor实际transform。原构建脚本中的初始数值已不是全部终态；后续贴合由 `probe_supports.py`、`refine_supports.py` 与 `SupportProbes.json` 记录。射线贴合避免仅靠包围盒高度猜测倾斜层板或车床的承托位置。

## 碰撞与旧代理

56个被替换旧Actor仍保留，关闭可见性、游戏内显示和阴影投射；未删除源节点。

| 隐藏代理 | 数量 | 当前碰撞与导航 |
|---|---:|---|
| `BridgeDeck_0`…`BridgeDeck_16` | 17 | 保留原连续桥面碰撞，继续承担玩家走桥表面 |
| 制作台原桌面/腿/下层板/碗 | 7 | 关闭碰撞，禁止继续影响导航 |
| 仓储原架/箱/箱盖/修理桌/托盘 | 32 | 关闭碰撞，禁止继续影响导航 |
| 合计 | 56 | 17保留、39关闭 |

新60个实例中，19个为 `QueryAndPhysics`，41个为 `NoCollision`。新桌、架、箱、桶、长凳、小车等按实际网格提供阻挡；复制网格在 `build_village.py` 设置 `Use Complex Collision As Simple`，避免单一包围盒填死桌腿或货架间空隙。小件、袋筐、板件和新桥面外观不承担碰撞。本轮不为小车或桶启用物理模拟。

桥面可见层是 `FV__BridgeWoodDeck0/1/2`，使用 `SM_PROP_walkway_wood`，每段约4.2×5.7×0.31m，沿原桥线排列。3个新桥面实例均无碰撞；下面17个隐藏桥板才是当前连续碰撞表面。程序修改桥时要同时识别外观与碰撞层，不能因桥板隐藏就删掉旧17块，也不能给新桥面再叠一层未知碰撞。

原 `BridgeUnderpassFloor`、`BridgeUnderpassWall*`、`BridgePier*`、`BridgeAbutment*`、`BridgeContact_*` 继续保持，桥下穿行和两岸接土体仍按之前的结构定义。桥面与桥下运行站位见[Runtime.json](Runtime.json)，基地近桌往返行走见[WalkRuntime.json](WalkRuntime.json)；这些实际证据与静态导航检查分开记录。

## 屋顶遮挡与镜头

基地独立弧形屋顶、分离的上墙、仓架独立顶棚、废墟残屋顶及原相机渐隐逻辑继续使用。本次60个新摆件的fade标记均为false；没有新建整体透明家具或把整栋房子合并成一个渐隐件。

固定2.5D相机、跟随参数及玩家相关蓝图/C++不因本轮美术替换改变。若后续需要把高家具加入渐隐，应独立标记组件并检查碰撞持续存在，不能以隐藏整栋建筑来代替当前局部渐隐方案。

## 保存、回退与证据

`VillagePass/L_DoughWorld_Depth_BeforeVillage.umap` 与 `VillagePass/PlacementManifest_BeforeVillage.json` 是本轮前快照。当前场景继续保存在原Depth关卡；旧V2对照地图不在本次修改范围。回退需要在关闭对应关卡、确认项目状态后协调恢复地图与Manifest，不要只把隐藏旧几何全部显示出来，否则会与60个新实例重叠。

`build_village.py` 是这次实施脚本，不是可随意重复运行的“刷新”按钮；它检测已有VillagePass标签并阻止重复执行。参数或资源替换应基于当前Actor和BuildReport做增量处理。

终态[EditorVerification.json](EditorVerification.json)已记录1171个Actor、50个源锚点XY保持、20项导航状态各自符合预期，导航构建结束。原地图包含按设计不连通的点，因此“20项符合预期”不代表20条全部可走。

[VillageVerification.json](VillageVerification.json)的passed=true：60个新实例位置/缩放/网格/碰撞和56个隐藏代理核对通过，50个原节点XYZ相对修改前最大差值为0cm。[FinalPersistence.json](FinalPersistence.json)的passed=true：1171Actor、1110条记录、6600植被实例；当前关卡已保存，缺失Actor、网格、材质和环境挂接问题列表为空。该文件记录本次保存与状态核对，不额外承诺未记录的打包或跨工程迁移测试。

[Runtime.json](Runtime.json)包含8个真实PIE站位与截图，全部grounded=true，镜头FOV55、臂长2800cm，角度约−50°/45°保持。[WalkRuntime.json](WalkRuntime.json)中6段基地往返实际行走均reached=true、error=null；它验证场景通行，不实现资源拾取或库存。[CameraFadeRuntime.json](CameraFadeRuntime.json)的6项组件/参数检查通过、error=null；脚本明确未做逐像素透明度验证，也未单独证明渐隐前后碰撞不变。

完整交付入口、截图及测试范围见[README.md](README.md)。本轮资源替换没有新增游戏机制。
