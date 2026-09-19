# Fantastic Village：现有面团世界地图的局部美术替换建议

日期：2026-09-13。此文档是依据磁盘实际文件名和既有 PlacementManifest 编制的选型建议；最终采用、网格尺寸、材质依赖及运行验证由现场 UE 检查决定。**用户明确不喜欢 Mushroom 资产包，本轮不再推荐、购买或下载它。**

本文件保留选型阶段的候选思路。**最终实际采用30种网格、60个新增实例，当前Manifest共1110条记录；已实施项、实例数、碰撞与射线贴合修正请见 `EnvironmentAssetList.md`、`Integration.md` 和 `BuildReport.json`。** 下面尺寸和替换方式属于原选型建议，不能替代最终记录。最终交付与运行证据入口见[README.md](README.md)。

## 修改方向

在现有 `/Game/Maps/DoughWorldDepth/Levels/L_DoughWorld_Depth` 上做基地、废墟、桥与生活道具升级。保留面团房体、弧形屋顶、现有 SoStylized 地形岩石与分区草，使用 Fantastic Village 的木器、面包、袋筐、零散木构来增加生活感。整栋 `BP_BLD_house_1…14`、城墙塔楼和包内全局天空/景观蓝图不进入本轮方案。

选型开始前的 Manifest 基线有1,050条放置记录、50个源锚点。本方案不改变原50个节点的XY、路线、地形或入口，不新建另一张地图。原V2对照关卡继续保留。用户拒绝新的 Mushroom 包，不代表本次自动删除现有场景中的菌类占位结构。

## 已找到的实际资源

磁盘包根目录：`E:/Unreal Project/GDATtest/Content/Fantastic_Village_Pack`，原UE包根：`/Game/Fantastic_Village_Pack/`。本轮只读清点为 **642 个 .uasset，其中444个文件名以 SM_ 开头**；这不等于642个资源已加载正常。以下均确有对应文件，完整源包路径和磁盘路径见同目录 `AssetPlan.json`。

| 优先级 / 用途 | 实际网格名（简写） | 区位与用法 |
|---|---|---|
| 1 制作台 | `SM_PROP_table_01`；备选02/03 | `jidi_03`：替换现有桌面、腿和底板的可见几何，保持221.7/266.7与2.8×1.35m占地 |
| 1 制作碗 | `SM_PROP_bowl_wood_01`；备选02 | 对齐原mixing_bowl，保留面粉团和擀面杖 |
| 1 面包 | `SM_PROP_food_bread_01`、03、05；`SM_PROP_plate_wood` | 制作台边摆2–3块，直接强化面团世界主题，不新增资源点 |
| 1 仓储货架 | `SM_PROP_market_shelf_01`；备选02/03 | `jidi_04`：原196.7/256.7附近，替换木架外观，保留独立顶棚 |
| 1 箱与箱盖 | `SM_PROP_box_02` + `SM_PROP_box_lid_02`；备选03配对 | 对应原crate_0/1/2，保持盖子独立；实际匹配关系需UE确认 |
| 1 袋与篮 | `SM_PROP_sack_01`、02、03；`SM_PROP_basket_01` | 货架底层和侧后方，建议2袋1篮；“面粉袋”是场景用途命名 |
| 2 修理桌 | `SM_PROP_table_02`、`SM_PROP_board_01`、`SM_PROP_cart_wheel_small` | 修理桌仍在200.2/256.7；少量板件/轮件说明维修用途 |
| 2 基地生活细节 | `SM_PROP_bench_01`、`SM_PROP_stool_01`、`SM_PROP_lantern_01`、`SM_PROP_bucket_01` | 房内后墙与入口侧墙，中心动线留空，不引入交互 |
| 2 门框木饰 | `SM_BLD_beam_small_v01_01`、`SM_BLD_beam_small_v01_02` | 仅适配原门柱和横楣，保持门洞、面团墙、独立弧顶 |
| 1 废墟零散木构 | `SM_BLD_beam_small_v02_01`、`SM_PROP_planks_01`、02 | `meijun_03`：现有西/北断墙和废墟边缘倒伏布置，不造整栋住宅 |
| 2 废墟运输残件 | `SM_PROP_cart_wheel_small`、`SM_PROP_crate_01`；整车`SM_PROP_cart_01`仅备选 | 既有off_floor_rubble外侧小组合；这些文件名不代表模型自带破损状态 |
| 1 桥面饰面 | `SM_PROP_planks_01`、02、`SM_PROP_walkway_wood`、小木梁 | `meijun_06`：沿现桥4.2m宽、约17.06m长范围适配，保留桥洞与两岸 |
| 3 整段木桥备选 | `SM_PROP_bridge_wood_01`至04 | 只在真实尺寸/净空适配时替代上项；不改变桥线或叠出第二座桥 |

## 建筑与桥的边界

基地原房体约9×7m，入口门槛2.45m宽；新增家具靠墙或靠边，保留正门到房内的连续通道。制作台和修理桌原桌面/下层板无碰撞、窄腿有碰撞，不能换成一个实心碰撞长方体。建议新网格先只负责外观，旧有窄腿碰撞代理保留；验证后再决定是否细化碰撞。

仓储箱盖、顶棚，以及基地屋顶/上墙继续独立。完整货摊或住宅网格若把顶棚、柱、墙合成不可分别处理的构件，就选择开放式家具备选，不牺牲目前的局部遮挡渐隐。

桥的原锚点是 `(393.3,170)`，桥面沿Y方向。Manifest桥板底部约−3.8911m、顶部约−3.6111m；最终以UE当前关卡实测为准。保留 `BridgeUnderpassFloor`、`BridgeUnderpassWall*`、`BridgePier*`、`BridgeAbutment*` 与全部 `BridgeContact_*`，沿原桥面替换外观。不得在旧表面上同高叠层产生闪烁；不得让完整木桥的栏杆、桥拱挤占已验证的桥上/桥下路径。

## 接入和验证

所有本轮新复制或派生资源放在 `Content/Maps` 下，建议根为 `/Game/Maps/Fantastic_Village_Pack/` 并保留原相对目录；最终路径由主任务现场操作记录。使用编辑器的依赖感知迁移/复制，不能只把个别uasset搬走后认为材质已齐全。不更换全局天空、灯光、Landscape材质或草体系；暂不使用RVT。

`AssetPlan.json` 包含每项真实源路径、备选、原有Actor标签/路径、源锚点、尺寸和采用条件。这是人工复核用的建议，**不是自动执行脚本，也不是已替换完成报告**。现场应重点核查：材质父级与纹理依赖；实际网格朝向、尺度、枢轴和箱盖配对；50个节点XY保持；基地/制作台/废墟/桥上桥下行走；2.5D相机下屋顶渐隐与小道具可读性。保存后再检查引用及挂接。

本轮仅让原有地点更容易辨认。食物、箱子、灯笼、座椅均为环境摆件；不新增拾取、制作、坐下、开箱、发光控制或压板桥机制。
