# Fantastic Village 原地图修改完成说明

2026-09-13，UE 5.8.2。已直接修改并保存原地图 `/Game/Maps/DoughWorldDepth/Levels/L_DoughWorld_Depth`。工程仍为 `E:/Unreal Project/GDATtest/GDATtest.uproject`，内容浏览器进入 **Maps → DoughWorldDepth → Levels** 打开该关卡即可。本轮没有新建另一张地图。

## 本次变化

- **制作区**：木制工作台、搅拌碗、面包、餐盘、勺子、面粉袋、凳子和水桶。
- **仓储维修区**：开放式货架、木箱与独立斜放箱盖、袋筐、桶、修理桌、木板和轮件。
- **基地生活区**：内外长凳、边桌、食物和容器；入口与房间中央留出通路。
- **运输补给**：小车、车上货物及车旁物资。
- **废弃聚落**：沿断墙布置散落木板、旧货架、箱桶、车轮与停放的小车。
- **桥梁**：用木桥板替换简易桥面的外观，保持原桥线、桥上碰撞、桥洞和两岸结构。

采用 **30种网格、60件新实例**。56个旧部件保留在关卡中并隐藏：其中17块桥板继续承担连续碰撞，其余39个代理关闭碰撞。可在大纲 `Depth/Village` 查看新摆件，旧代理在 `Depth/Village/ReplacedProxyGeometry`。新摆件有19件启用静态精确碰撞，41件小装饰或桥面饰面无碰撞。

保留原面团建筑、独立屋顶和菌类组合造型，继续使用现有SoStylized环境；没有使用用户排除的Mushroom包。原50个锚点的XYZ与本轮开始前完全相同，原地形和6,600个原生植被实例保留。11项高度/尺寸修正让货架上的袋筐、箱子和室内面包贴合实际支撑面；车上货物另按车床射线定位。

## 资源与程序对接

新网格副本：`/Game/Maps/DoughWorldDepth/Village/Meshes`。它们引用现有 `/Game/Fantastic_Village_Pack` 的14种材质实例及后续依赖；**迁移时必须通过UE迁移功能带上依赖，不能只复制Village/Meshes**。原资产包保留，没有改写其母材质、纹理或导入演示关卡。

本轮没有修改蓝图/C++、相机或按键：鼠标左键移动，Space跳跃，Shift翻滚，既有深渊入口E请求、Enter确认。新增食物、袋筐、家具和小车属于环境摆件，不包含采集、制作、开箱、乘车等机制。

- [逐项环境资产清单](EnvironmentAssetList.md)：30种网格、实际数量、用途、碰撞和材质依赖。
- [程序对接说明](Integration.md)：锚点、代理保留方式、可交互资产接入边界。
- [实际变更记录](BuildReport.json)：源路径、新路径、60个实例、56个替换代理及高度修正。
- 当前完整场景清单位于上级 `PlacementManifest.json`：**1,110条记录**；场景共 **1,171个Actor**，两者统计口径不同。

## 本轮实际验证

| 检查 | 结果与证据 |
|---|---|
| 原包导入 | UE登记645项资源，444静态网格；缺失包依赖、空材质父级、空纹理覆盖、空网格材质槽均为0。见[ImportAudit.json](ImportAudit.json)。选用资源另经缩略图与运行画面检查。 |
| 场景一致性 | 60个新实例的网格、尺寸、位置与碰撞符合清单；56个旧代理隐藏与碰撞状态正确；50源锚点最大位置变化0。见[VillageVerification.json](VillageVerification.json)。 |
| 导航 | 20项查询全部符合预期，包含16条可达路线及4条应封闭路线。见[EditorVerification.json](EditorVerification.json)。 |
| 实际玩家视角 | 基地内外、制作台、仓储、小车、废墟、桥上及桥下8站均落地。相机Pitch=-50°、Yaw=45°、FOV=55°、臂长2800cm。见[Runtime.json](Runtime.json)与Captures。 |
| 实际移动 | 面粉点与基地设施之间6段往返全部到达，79个移动采样全部落地。见[WalkRuntime.json](WalkRuntime.json)。最后新增的车内货袋无碰撞，不改变该行走检查的路径条件。 |
| 遮挡渐隐 | 6组检查通过，屋顶保持局部渐隐。见[CameraFadeRuntime.json](CameraFadeRuntime.json)。 |
| 保存与引用 | 当前关卡已保存；1,110条记录无缺失Actor或网格错配，材质与环境组件挂接无异常，原生植被6,600。见[FinalPersistence.json](FinalPersistence.json)。 |

这是当前编辑器中的保存及PIE验证，本轮没有关闭重开整个编辑器、重新打包EXE或穷举每条跳跃/翻滚路线。渐隐测试包含动态材质异常输入，日志可见其MID父材质警告；导航Python查询也触发既有辅助诊断堆栈。它们不等于原包导入缺失，测试结果和保存场景引用另按以上文件核对。

## 查看效果与回退

![制作台与运输区](Captures/03_Workbench.png)

![仓储区](Captures/04_Storage.png)

![桥上](Captures/07_BridgeDeck.png)

修改前关卡备份为工作区同目录 `L_DoughWorld_Depth_BeforeVillage.umap`，原清单为 `PlacementManifest_BeforeVillage.json`。两者用于整轮回退；不要在编辑器打开目标关卡时覆盖uMap文件。构建与细调脚本用于记录本轮实施过程，已完成的关卡不要重复运行构建脚本叠加摆件。

项目内另有本说明、清单、验证和截图的归档：`Content/Maps/DoughWorldDepth/Documentation/VillagePass`；本轮源脚本与数据归档到 `Content/Maps/DoughWorldDepth/Source/AuthoringProject/VillagePass`。
