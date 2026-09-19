# 程序与蓝图对接说明

> 2026-09-13 更新：已在本关卡完成Fantastic Village局部美术替换，采用30种网格、60件新摆件；当前1,110条布置记录、1,171个Actor，原50锚点和6,600株植被保留。最新截图、运行验证与资产对接见 [VillagePass/README.md](VillagePass/README.md)。以下保留原地形阶段说明。

**场景与接口已接入并完成所列运行验证。** 本文依据2026-09-10续接后的源码和实际JSON更新。最新角色代码完整构建成功，六区镜头、三次实际往返、局部渐隐、主动入口、跳跃翻滚及真实GameMode换玩家均已复跑。最终保存重开和归档记录统一见TestResults.md。

## 资源与原生代码位置

| 内容 | 路径 |
|---|---|
| 候选关卡 | `/Game/Maps/DoughWorldDepth/Levels/L_DoughWorld_Depth` |
| 保留原关卡 | `/Game/Maps/DoughWorldV2/Levels/L_DoughWorldV2_Desert` |
| 候选材质 | `/Game/Maps/DoughWorldDepth/Materials` |
| 自建静态网格 | `/Game/Maps/DoughWorldDepth/Meshes/SM_DW_*` |
| 地形资源 | `/Game/Maps/DoughWorldDepth/Terrain` |
| 低植被类型/材质 | `/Game/Maps/DoughWorldDepth/Foliage/Types`、`Foliage/Materials` |
| 源数据归档 | `Content/Maps/DoughWorldDepth/Source`；归档结果见TestResults.md |
| 对接文档 | `Content/Maps/DoughWorldDepth/Documentation`；归档结果见TestResults.md |
| C++参考副本 | `Content/Maps/DoughWorldDepth/Source/NativeCodeReference`；便于美术/程序交接，不参与编译 |
| 真正编译源码 | `E:/Unreal Project/GDATtest/Source/GDATtest/`；修改这里并重新构建才会改变原生行为 |

新增 Native 类型为 `UCameraOccluderFadeComponent`、`ADepthEnvironmentActor`、`ADepthInteractionFocusActor`、`ADepthAbyssPortalProxy`、`UDepthAbyssPromptWidget`、`UDepthLandscapeTools`。对应 5 组 .h/.cpp。实际 `GDATtest.Build.cs` 已包含 Landscape；其余现有 Engine、InputCore、UMG、Slate 等依赖继续使用。新增反射类需要完整 UHT/C++ 构建，不把 Live Coding 状态当成完整构建证明。

## 功能由谁负责

| 功能 | 对应蓝图/代码 | 交接时修改位置 |
|---|---|---|
| 选择默认玩家和控制器 | `BP_TopDownGameMode` | 沿用现有Default Pawn和Player Controller配置，不另建一套地图玩家。 |
| 点击移动和固定俯视镜头 | `BP_TopDownController`、`BP_TopDownCharacter` | 保留现有输入、相机臂和镜头参数。 |
| Space跳跃、Shift翻滚 | 角色父类 `TopDownActionCharacter.h/.cpp` | 参数在角色蓝图类默认值的Player Actions分类；碰撞、朝向、冷却由原生方法处理。 |
| 屋顶、菌盖和上墙局部渐隐 | `DepthEnvironmentActor.h/.cpp` + `CameraOccluderFadeComponent.h/.cpp` | 独立网格组件标签、专用材质的CameraFade标量，以及渐隐组件参数。 |
| 附近建筑/资源的遮挡保护 | `DepthInteractionFocusActor.h/.cpp` | 当前是最近锚点代理；正式交互系统以后传入真实选中目标。 |
| 深渊E→Enter确认及提示 | `DepthAbyssPortalProxy.h/.cpp`（含提示Widget） | 场景中的入口范围、目标Actor、确认流程；正式过场以后接入。 |
| 外部遮罩导入Landscape | `DepthLandscapeTools.h/.cpp` | 编辑器工具接口；不在游戏运行时重建地形。 |

地图资源、素材和参考源码全部归档到Content/Maps。UE的原生构建必须从工程Source读取C++，因此同时保留真实编译版本和Content内参考副本；移交时应核对两者哈希，避免只修改副本。

## 角色与输入

继续使用 `/Game/TopDown/Blueprints/BP_TopDownGameMode`、`BP_TopDownCharacter` 与 `BP_TopDownController`。`TopDownActionCharacter` 负责现有 Space/Shift 行为，不向场景添加另一套玩家。

运行候选地图后先点击游戏视口取得焦点。鼠标左键点击地面移动；Space按下调用`StartActionJump()`、松开调用`StopJumping()`；左/右Shift调用`StartForwardRoll()`，方向取开始时角色朝向。深渊E/Enter/Esc属于入口代理，不是全地图通用采集按键。

| 参数 | 当前 C++ 默认值 | 说明 |
|---|---:|---|
| JumpLaunchSpeed | 600 cm/s | 起跳速度；不是本轮实测跳高。 |
| RollDistance | 400 cm | 目标翻滚距离；开阔地本轮实测400.089cm，碰撞可提前终止。 |
| RollDuration | 0.45 s | 翻滚持续时间。 |
| RollCooldown | 0.35 s | 翻滚结束后冷却。 |

相机基线：Perspective，Pitch=-50、Yaw=45、Roll=0，FOV=55，TargetArmLength=2800 cm，SpringArm absolute rotation=true、do_collision_test=false。只读实际 PlayerCameraManager，不修改上述参数，不新增缩放。

修复后16个PIE站位与6段实际往返均记录相机基线。ActionsLifecycleRuntime的8项检查通过：采样跳高118.333cm并落地、翻滚400.089cm并恢复输入/姿态；RollCollisionRuntime的3项检查又证明角色胶囊会在真实阻挡墙前停止，停止点误差约1.021cm，相机设置不变。测试调用这些真实按键绑定方法，没有自动注入硬件按键。

本轮还修正了已有TopDownActionCharacter.cpp的翻滚结束判断：以CharacterMovement根运动源的实际时间与结束状态为准，并允许最后一帧按剩余时长处理，避免低帧率下第二套计时提前结束或超走。原默认距离/时长/冷却不变。NativeBuild.log记录完整构建Succeeded、5.17秒；实际位移由运行测试确认，非测试脚本补偿。翻滚外观仍是网格旋转代理，不是专用动画Montage。

## 局部环境渐隐

`ADepthEnvironmentActor` 自带 `EnvironmentMesh` 与 `CameraFadeComponent`。只给独立菌盖、屋顶、洞顶片和上半墙使用组件标签 `CameraFade`；这是 StaticMeshComponent 标签，不是 Actor 标签。低墙、地面、孔边和强制门持续可见，碰撞不随淡出改变。

候选材质为 Masked，标量 `CameraFade` 默认 1，接入实际 OpacityMask；有原镂空 Alpha 时先保留再相乘。每个受控材质槽使用独立 MID，避免同网格其他 Actor 受影响。ISM/HISM 明确不使用该方案，低草继续走原生 Foliage。

| 渐隐参数 | 默认值 | 用途 |
|---|---:|---|
| ScanInterval | 0.08 s | 遮挡检测间隔。 |
| OccludedVisibility | 0.2 | 被遮挡时保留原材质可见度的20%；原基线0.7时为0.14。 |
| FadeOutSeconds / FadeInSeconds | 0.18 / 0.3 s | 淡出和恢复用时。 |
| ClearHoldSeconds | 0.2 s | 遮挡离开后稍等再恢复，减轻边缘闪烁。 |
| BoundsPaddingCm | 10 cm | 检测网格包围盒的外扩范围。 |
| TargetEndInsetCm | 8 cm | 在目标前收短检测段，减少把目标脚下支撑面判作遮挡。 |
| AdditionalTargetMaxDistanceCm | 650 cm | 额外目标检测距离；当前焦点代理先限制在500cm内。 |

地下DeepCeiling洞顶片与DeepHangingLobe悬垂体在场景中单独覆盖BoundsPaddingCm=650、OccludedVisibility=0.10，以改善固定镜头附近顶片的可读性；普通墙、屋顶、菌盖仍保持各自原设置。该覆盖不修改碰撞。

替换网格/材质后调用`RefreshFadeMeshes()`；检查用`EvaluateOcclusionNow()`、`GetFadeStatus()`、`GetViewSnapshot()`。CameraFadeRuntime最新6组数值行为全部通过；SceneFadeComparison在等待原有camera lag稳定后重拍，前后相机位置差0、角度/FOV/臂长相同，碰撞相同，没有关闭相机平滑。

当前交互可读性由 `ADepthInteractionFocusActor` 临时按 5 m 内最近的真实建筑/资源锚点选择焦点，再集中调用 `SetInteractionTargetForWorld`。它没有正式“选中交互目标”的玩法语义。正式交互系统接入后，应在目标变化事件中调用：

`UCameraOccluderFadeComponent::SetInteractionTargetForWorld(WorldContextObject, TargetActor, LocalOffset, RadiusCm, HalfHeightCm)`

传 nullptr 清空。集中控制，避免与最近目标代理同时改写。默认保护点为锚点局部 (0,0,80) cm，半径 60 cm、半高 90 cm。正式交互系统接入时仅登记实际可交互建筑/资源；无交互的废弃聚落不应登记。源锚点 HiddenInGame 必须 false，否则当前组件会跳过；TargetPoint 的编辑器图标不等于必须隐藏整个 Actor。

## 深渊入口代理

`ADepthAbyssPortalProxy` 绑定 `holes/hole_abyss`。场景覆盖 InteractionRadiusCm=1350，区别于类默认 350 cm；这是为大孔安全接近区配置的代理范围。

E 调用 `RequestEntryConfirmation()`，Enter 调用 `ConfirmEntry()`，Esc 调用 `CancelEntry()`。范围外、未配置目的地、腾空或翻滚时不能开始；确认前不会传送。成功调用带碰撞检查的 `TeleportTo`，并清除旧点击路径和速度；保持相机。落点阻挡时失败留在源侧。

优先设置 `UndergroundDestinationActor` 为角色胶囊中心落点；备用 `UndergroundDestination` 使用 UE 世界厘米且要求 `bDestinationConfigured=true`。当前场景地面落点为源图 (411.7,109.3) 米、Z=-60 米；目的 TargetPoint 在此处上方约 1.05 米，最终以 `DepthOverrides.json` 为准。

诊断：`player_in_range`、`awaiting_confirmation`、`confirmed_entry_count`、`last_entry_error`。没有正式过场、钥匙判断或回程出口，不能称深渊完整玩法已完成。

## 地形开孔与坐标

源图 XY 为米。UE_X=(x−250)×100，UE_Y=(y−250)×100，UE_Z=z×100。14个自建几何使用底部中心pivot；生成器`scene_common.piece()`将传入Z作为底面，并按实际网格包围盒换算Actor位置，兼容其他pivot的SoStylized/基础形状。物件局部偏移与源锚点分开记录。

505×505 高度图覆盖 500 m，Landscape 位置 (-25000,-25000,0) cm，Scale=(99.2063492,99.2063492,100)。R16 为无符号 16 位小端，value=round(32768+z_m×128)。16 位 PNG 与 R16 逐像素一致；HeightRG 的 R/G 分别承载高/低字节。

高度图承载地表；真实孔、桥下、悬挑和洞顶依赖补充网格与 Landscape Visibility。`DepthLandscapeTools.import_visibility_mask(landscape, render_target, 0)` 仅允许候选目录且非 PIE；红通道 0 实地、1 孔洞。材质需要 LandscapeVisibilityMask→OpacityMask。返回成功只代表导入接受，最终仍需穿孔射线、碰撞和重载验证。

中空孔环已导入为静态复杂碰撞。不得替换成整环单个凸包，否则会封死孔口。静态几何使用复杂碰撞不等于支持物理模拟；未来可推方块的物理碰撞需另外设置。

## 场景和增补数据

`PlacementManifest.json` 是实际对象快照；`DepthOverrides.json` 存总体新增高度/入口；`TerrainDesign/depth_supplement.json` 存原锚点及新增地形约束。`Planning/ModulePlan.json` 是设计计划，不能代替实际记录。

场景 Actor 标签 `DepthBuilt` 标识本轮生成对象，`Module:<id>` 对应 44 类模块，`Source:<source_ref>` 对应原数据。原锚点 TargetPoint 标签为 `OriginalSource:<ref>`。后续资产替换应保持原锚点与模块关系，并更新实际对象表。全量生成脚本会清理本候选中带 DepthBuilt 的对象，适合受控重建，不应在用户手工改图后无检查重跑。

后续仍待正式接入：形态、战斗/敌对、AI、掉落、采集、背包、建造、可破坏门、压力板桥、呼吸孔、气孔近道和坍塌孔。占位资产不冒充这些系统。

## 当前集成修正

scene_common 对 `fade=True` 且 `collision=False` 的部件设置 `cast_shadow=False`，避免屋顶已淡而浓重投影仍遮住角色。建筑/资源 TargetPoint 在point()中显式`HiddenInGame=False`。当前生成器按buildings/resources类别收集空间锚点，这不等于正式交互资格筛选；正式系统接入时应仅传入真正可交互对象，避免把废弃聚落当作选中目标。

本轮定位到47个环境子网格从Actor脱挂：根组件为Movable、子网格为Static，在重注册时组合不合法，导致子网格落到世界原点，出现巨大错位体块并阻断教程导航。限定修复已恢复47项挂接及变换，20项导航预期全部匹配。

最新`DepthEnvironmentActor.cpp`已将固定环境的SceneRoot和EnvironmentMesh同时设为Static，再建立挂接；仅瞬时PIE验证演员允许同时改为Movable。`scene_common.py`也先设置根移动性。本次已完成这版代码的完整构建并重开候选，ResumeEnvironmentAudit的146项挂接与变换一致、bad为空；后续动作/换Pawn测试也通过。最终编辑保存状态见TestResults.md。

地下5盏局部灯由38000降为2800，FinalPolish.json记录数值；修复后的完整六区镜头已更新。桥洞新增5件底层/接土体，BridgeContactFit只进一步降低两侧背板顶部以贴合地表，两端保持较低高度，原源锚点不变；BridgeRuntime已重拍桥上和桥下，角色均落地、相机基线一致。不要无检查重跑全量生成器覆盖当前修复和用户后续改图。

## 源关联元数据说明

PlacementManifest 已补齐 region/category_cn。原有明确 SourceID 保持；原来源空白的新装饰使用 source_ref=regions/<区域>，并标记 source_association_kind=additive_region_reference、source_ref_was_empty=true。这只补原区域的设计关联，不改Actor路径或位置，不生成新原始节点。Landscape活动地面旧字段original_ground_layout保留，同时补source_anchor_ref=regions/<区域>。

## 玩家生命周期对接证据

ActionsLifecycleRuntime保留真实事件顺序：UnPossess旧C_0后将其销毁，当前控制器自动生成同类玩家C_1；测试再对控制器明确UnPossess，并调用实际GameMode.RestartPlayer，创建新的同类玩家C_2。两者均为BP_TopDownCharacter，不把自动新玩家误写成Spectator。

旧玩家移除后，原位置受影响的材质恢复；C_2稳定落地、保持相机基线且输入未被锁住，已有环境渐隐会跟随新玩家。该测试证明接口生命周期，不提供生命值、死亡判定、掉落或复活规则。正式死亡/复活系统应使用项目真实控制器和GameMode，并保留这套验证方式。

交付证据为真实游戏截图、轨迹JSON和构建日志，没有流程录像。运行日志中的NavigationSystem类默认对象handled ensure属于Python诊断路径；不能宣称零警告，具体日志状态见TestResults.md。

## 已保存的导入与焦点设置

实际焦点数组包含主棚、工作台、储物、核心祭坛和4个资源锚点，共8项；废弃聚落已排除。生成脚本是制作参考，普通打开无需执行。Source与Documentation目录已设为当前工程自动导入的排除项；真实材质和网格目录仍保留正常导入行为。
