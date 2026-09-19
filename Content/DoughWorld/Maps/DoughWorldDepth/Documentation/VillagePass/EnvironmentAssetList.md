# Fantastic Village 本轮实际环境资产清单

记录依据：`BuildReport.json`，生成批次 `FantasticVillage_20260913`。本轮在原 `/Game/Maps/DoughWorldDepth/Levels/L_DoughWorld_Depth` 完成30种网格副本、60个新增实例，并隐藏56个被替换的旧几何代理。BuildReport记录 `saved=true`、`errors=[]`。本清单记录资产与依赖；终态检查和运行证据见下文及[本轮交付说明](README.md)。

## 保存位置与原包依赖

新增网格均位于 `/Game/Maps/DoughWorldDepth/Village/Meshes/<下表网格名>`。源资产路径逐项保存在 `BuildReport.json → new_assets[].source`，网格副本继续引用 `/Game/Fantastic_Village_Pack/materials/` 下的14种原包材质实例及其父材质、纹理依赖。本轮没有复制或改写母材质、没有引入RVT，也没有替换全局天空/景观材质。

`placements[].material=null` 表示放置时未设置自定义材质覆盖，不表示缺材质；实际槽位材质见同记录 `materials[]`。迁移或交付时须包含原Fantastic_Village_Pack依赖，不能只复制Village/Meshes。

## 实际使用的30种网格

下表材质名共同前缀为 `/Game/Fantastic_Village_Pack/materials/`。碰撞Q表示实例启用 `QueryAndPhysics`；无表示 `NoCollision`，不作阻挡。复制网格在构建脚本中设为 `Use Complex Collision As Simple`，供静态环境查询使用。

| 新网格名 | 实例数 | 用途 | 碰撞 | 原材质实例 |
|---|---:|---|---|---|
| `SM_PROP_table_02` | 1 | 制作台主桌 FV__Workbench | Q | MI_wood_planks_03 |
| `SM_PROP_bowl_wood_01` | 1 | 制作台搅拌碗 | 无 | MI_wood_03 |
| `SM_PROP_plate_wood` | 1 | 制作台面包托盘 | 无 | MI_wood_07 |
| `SM_PROP_food_bread_02` | 1 | 制作台面包 | 无 | MI_PROP_food |
| `SM_PROP_food_bread_05` | 1 | 制作台小面包 | 无 | MI_PROP_food |
| `SM_PROP_spoon_01` | 1 | 制作台勺子；实际原材质为金属 | 无 | MI_metal_01 |
| `SM_PROP_sack_01` | 5 | 制作台/仓架/补给车旁及车内袋装原料 | 无 | MI_cloth_01 / MI_PROP_food |
| `SM_PROP_sack_02` | 2 | 制作台/仓架袋装原料 | 无 | MI_cloth_01 / MI_PROP_food |
| `SM_PROP_sack_03` | 3 | 制作台/仓架/补给车旁袋装原料 | 无 | MI_cloth_01 / MI_PROP_food |
| `SM_PROP_stool_01` | 1 | 制作台侧凳 | Q | MI_wood_06 |
| `SM_PROP_bucket_01` | 2 | 制作台水桶、基地屋内木桶 | 无 | MI_wood_planks_07 / MI_metal_02 |
| `SM_PROP_market_shelf_03` | 1 | 仓储主货架 | Q | MI_wood_planks_05 |
| `SM_PROP_box_02` | 4 | 仓储箱3件、废墟开口箱1件 | Q | MI_wood_planks_01 / MI_wood_planks_03 / MI_PROP_food |
| `SM_PROP_box_lid_02` | 3 | 仓储侧靠箱盖2件、废墟侧靠箱盖1件 | 无 | MI_wood_planks_01 / MI_wood_planks_03 |
| `SM_PROP_basket_01` | 6 | 仓架篮3件、基地篮2件、废墟篮1件 | 无 | MI_rope |
| `SM_PROP_table_01` | 1 | 修理桌 | Q | MI_wood_planks_05 |
| `SM_PROP_planks_03` | 1 | 维修桌台面木板 | 无 | MI_wood_planks_05 |
| `SM_PROP_cart_wheel_small` | 2 | 维修区与废墟车轮 | 无 | MI_wood_07 / MI_metal_02 |
| `SM_PROP_barrel_01` | 5 | 仓储/补给车/废墟/桥两端木桶 | Q | MI_wood_planks_03 / MI_metal_02 |
| `SM_PROP_sack_05` | 3 | 仓储与桥端袋装补给 | 无 | MI_cloth_01 |
| `SM_PROP_sack_06` | 1 | 仓储袋装补给 | 无 | MI_cloth_01 |
| `SM_PROP_bench_01` | 2 | 基地屋内后墙、屋外前侧长凳 | Q | MI_wood_planks_05 |
| `SM_PROP_table_03` | 1 | 基地屋内边桌 | Q | MI_wood_planks_05 |
| `SM_PROP_food_bread_08` | 1 | 基地边桌面包 | 无 | MI_PROP_food |
| `SM_PROP_cart_01` | 1 | 基地补给车 | Q | MI_wood_07 / MI_metal_02 / MI_wood_detail_01 |
| `SM_PROP_planks_01` | 2 | 废墟散落木板，第0/2组 | 无 | MI_wood_planks_05 |
| `SM_PROP_planks_02` | 2 | 废墟散落木板，第1/3组 | 无 | MI_wood_planks_05 |
| `SM_PROP_market_shelf_02` | 1 | 废墟低货架 | Q | MI_wood_planks_05 |
| `SM_PROP_cart_03` | 1 | 废墟侧方小车 | Q | MI_wood_07 / MI_metal_02 / MI_wood_detail_01 / MI_PROP_hay |
| `SM_PROP_walkway_wood` | 3 | 3段桥面外观；碰撞沿用原17块隐藏桥板 | 无 | MI_wood_planks_07 |

合计：**30种 / 60实例**，其中19实例启用QueryAndPhysics、41实例无碰撞。所有60实例的 `fade=false`；既有屋顶和上墙的独立渐隐组件保留，不把新家具整体加入透明处理。

## 按区域核对

| 模块 | 区域 | 新增实例 |
|---|---|---:|
| `jidi_03` | 制作台 | 11 |
| `jidi_04` | 仓储/修理/补给 | 24 |
| `jidi_02` | 基地居所 | 7 |
| `meijun_03` | 废弃聚落 | 11 |
| `meijun_06` | 桥及两端 | 7 |

基地建筑的面团房体、独立弧形屋顶，以及废墟断墙/残屋顶继续使用原场景结构。桥只替换为3段木走道外观，保留原桥下通道、支撑与岸体接触构件。本轮没有放置Fantastic完整中世纪住宅，没有使用Mushroom资产包。

## 实际承托面的贴合修正

最终 `support_refinements` 记录11项修正：4个货架原料袋、3个货架篮以及屋内面包，按目标真实网格射线命中高度加0.003m贴合；3个底层箱降低至底面Z=6.39m，并按0.88/0.94等比缩小，适配货架间净空。尺寸和高度采用最终BuildReport记录，不再用选型草案或初始摆放值。

另外新增 `FV__CartLoad0`，复用 `SM_PROP_sack_01`，对补给车真实车床进行独立射线后加0.005m放置。这使本轮总实例由59增至60，sack_01由4增至5；不增加新网格种类。最终Manifest为1110条记录（原1050+新增60），对应场景总计1171个Actor。

## 美术与机制的边界

面包、原料袋、桶、箱、箱盖、篮、家具、小车及维修零件都是环境资产。侧靠箱盖为独立摆件，不带开箱动画；车不带驾驶，食物不带拾取或制作产出，座椅不带坐下，原压板桥未因换外观而获得新机制。已有玩家移动、跳跃、翻滚、传送代理及其程序接口不在此次美术替换范围内。

`AssetPlan.md/AssetPlan.json` 保留本轮候选思路；实际网格、数量、碰撞和最终位置以 `BuildReport.json` 及当前关卡为准。例如最终制作台选table_02、仓架选market_shelf_03，未采用候选中的完整木桥。

## 终态证据

- [编辑器检查](EditorVerification.json)：1171个Actor，50个源锚点XY保持，20项导航状态各自符合预期，导航构建结束。
- [Village变更核对](VillageVerification.json)：passed=true；60个新实例的网格/位置/缩放/碰撞及56个隐藏代理核对通过，50个源锚点XYZ相对本轮修改前最大差值为0cm。
- [保存与依赖检查](FinalPersistence.json)：passed=true；1110条Manifest记录、1171Actor、6600植被实例；缺Actor、网格不一致、材质及环境挂接问题列表均为空，当前关卡已保存。
- [固定镜头运行采样](Runtime.json)：8个站位全部落地，FOV55、臂长2800cm，镜头角度保持约−50°/45°；附实际截图路径。
- [基地往返行走](WalkRuntime.json)：6段实际移动全部reached、error=null；验证范围是原面粉代理与基地设施间行走，不包括采集库存机制。
- [渐隐运行检查](CameraFadeRuntime.json)：6项组件/参数相关检查通过，error=null。该脚本明确不验证逐像素透明度，也不单独证明碰撞前后不变；不能扩大结论。

原包[导入检查](ImportAudit.json)记录645个资产登记项、444个静态网格；缺失Game依赖0、读取错误0；53个MIC空父材质0、67条纹理覆盖空值0、网格空材质槽0。这是依赖与字段核对，不等于逐材质编译或整个资产包全面验收。

使用入口、截图和完整测试范围统一见[README.md](README.md)。本轮完成的是现有地图的局部美术替换，环境摆件没有新增机制。
