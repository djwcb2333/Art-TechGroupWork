# 验证结果

> 2026-09-13 更新：已在本关卡完成Fantastic Village局部美术替换，采用30种网格、60件新摆件；当前1,110条布置记录、1,171个Actor，原50锚点和6,600株植被保留。最新截图、运行验证与资产对接见 [VillagePass/README.md](VillagePass/README.md)。以下保留原地形阶段说明。

本报告汇总2026-09-10续接后的实际证据。最新角色修复完整构建已成功（NativeBuild.log：5.17秒）；六区截图、走动、局部渐隐、入口、开孔及动作/换玩家测试均已重跑。最终保存重开及归档结果在末尾分别收口。不存在“仅完成原型”或“教程后4段仍因地形不可达”的当前结论。

## 数据、地形与场景

| 检查 | 结果 | 证据与范围 |
|---|---|---|
| 原件完整性 | 11/11哈希匹配 | Planning/IntegrityCheck.json；原JSON为d79b1bd9…7170809fd4。 |
| 原锚点XY | 50点保留 | EditorVerification.json，最大误差约2×10⁻¹⁴米；教程顺序未改。 |
| 实际模块 | 44类均有对象 | 最新PlacementManifest为1,050记录；5件新增桥洞接土体归meijun_06。 |
| 场景Actor | 保存前检查1,111 | FinalPersistence核对1,050条清单记录；包括锚点、灯光和辅助对象，和植被实例不是同一口径。 |
| 外部高度图 | 离线校验通过 | 505×505、500米；R16/PNG/RG编码一致；高程-14.060～15.755米，最大量化误差0.003906米。 |
| 教程道路坡度 | 离线8段通过 | 最大纵坡10.9686°，不代表整张地图最大坡度。 |
| 设施周边坡度 | 离线3窗口通过 | 14×14米窗口最大0.4289°；不代表正式建造占地系统已完成。 |
| 自建网格 | 14/14导入尺寸一致 | GeometryImportResult.json；真实UE包围盒等于源厘米尺寸。 |
| 原生低植被 | 6,600实例保存 | actual_low_foliage_report.json，类型计数一致、bad_tilt=0、bad_clearance=0，无RVT、NoCollision。 |
| 环境组件挂接 | 重开后146/146符合预期 | ResumeEnvironmentAudit.json，bad=[]；挂接和组件世界位置/旋转/缩放一致。 |
| 桥下接土 | 新增5件，边顶贴合地形 | SpatialPolish.json与BridgeContactFit.json；保留原桥位置和通路。 |
| 游戏地形碰撞 | 21/21射线符合预期 | HoleGameCollision_PIE.json：4孔、桥下、2实地控制点，all_passed=true。 |

编辑器旧Visibility射线可命中用于选择的无孔辅助高度场；旧HoleMaskDiagnosis中的false不能当作游戏孔洞失败。已改用游戏Camera和PIE Visibility检查，并包含实地控制点。

## 实际游戏运行

| 检查 | 最新结果 | 证据与范围 |
|---|---|---|
| 六区及重点站位 | 16/16落地 | FullRuntime.json（22:37），error=null；Captures/01–16为真实PIE玩家镜头。 |
| 固定相机 | 16处参数一致 | Pitch=-50°、Yaw=45°、FOV=55°、臂长2800cm；含桥上/桥下及地下。 |
| 桥上/桥下接土后站位 | 2/2落地 | BridgeRuntime.json（22:46），更新11/12截图，原相机基线一致。 |
| 翻滚撞墙 | 3/3检查通过 | RollCollisionRuntime.json：真实胶囊墙前停住，实际位移156.979cm，预期停止点误差1.021cm，姿态/输入恢复且相机设置不变。 |
| 三次设施往返 | 6/6段到达，57采样均落地 | WalkRuntime.json（22:44），error=null；只证明实际走动，不含采集和库存。 |
| 跳跃 | 起跳并落地 | ActionsLifecycleRuntime.json：采样最大高度118.333cm；采样并非连续高频理论峰值。 |
| 翻滚 | 约400.089cm，目标400cm | 同上；开阔地实际位移，结束后输入恢复、角色网格姿态恢复，grounded=true。 |
| 移除旧玩家 | 旧玩家已销毁且不再被控制 | 事件链记录旧C_0失效，控制器随后自动创建同类BP_TopDownCharacter_C_1。 |
| GameMode重建玩家 | 真实RestartPlayer创建C_2 | 测试先明确UnPossess，再调用当前GameMode.RestartPlayer，确认新实例在调用期间产生。C_1/C_2均为玩家角色类，不是Spectator。 |
| 新玩家与渐隐 | 相机/落地/控制状态正确 | 生命周期8/8项通过；旧目标渐隐恢复，新玩家重新驱动已有环境渐隐；未新增测试期间C++改动。 |
| 渐隐组件 | 6/6组检查通过 | CameraFadeRuntime.json（22:43），passed=true、error=null、cleanup_errors=[]。 |
| 场景屋顶对照 | 同机位、碰撞相同 | SceneFadeComparison.json（22:48），error=null、camera_position_delta_cm=0、same_collision=true。 |
| 深渊主动入口 | 5/5检查通过 | PortalRuntime.json（22:43），error=null；接近不自动进入、未请求拒绝、取消有效、确认进入并落地。 |
| 入口落点 | 单向进入地下 | entry_count=1，目的胶囊中心误差约0.85cm，grounded=true，相机参数相同。 |

渐隐6组涵盖多层遮挡、独立MID、原0.7基线的0.14渐隐及恢复、未标记组件跳过、额外交互目标、清空目标和外部材质保护、Deactivate恢复与相机不变。数值检查和场景图像/碰撞分别取证。

动作与入口验证执行的是真实Space/Shift和E/Enter绑定方法，没有自动注入硬件键盘事件。换玩家使用真实UnPossess、DestroyActor和GameMode.RestartPlayer；这是一项生命周期验证，不是正式死亡/复活玩法。

最终屋顶对照在相机位置连续稳定后拍摄，保留enable_camera_lag原设置，没有通过关闭平滑来锁定镜头。22:48的SceneFadeComparison前后世界位置差0，角度、FOV、臂长和碰撞均相同；此结果替代较早未稳定的对照。

## 导航结果

最新EditorVerification（22:48）已在nav_building=false时复核，20项查询全部符合预期，50个原点位XY不变、清单缺失0。旧22:28的构建中快照已由此结果替代。

| 查询组 | 实际/预期 | 结果 |
|---|---|---|
| Demo_1_2至Demo_8_9，共8段教程 | 可达/可达 | 全部匹配；原后4段已修复。 |
| BaseToHut、BaseToWorkbench、BaseToStorage、BaseToWall | 可达/可达 | 4项匹配。 |
| MoldDetour、BridgeCross、DeepEntryToAltar、DeepGuards | 可达/可达 | 4项匹配。 |
| ClosedBreakwall、FermentEastBypass、FermentWestBypass、YeastNorthBypass | 不可达/不可达 | 4项匹配；这是设计要求，不是导航失败。 |

后4段旧失败来自环境子网格脱挂到世界原点，形成错误导航障碍。47项限定挂接修复后路线恢复，最新构造代码让固定父子组件都为Static。曾用于定位问题的临时岩壁导航排除已恢复，未以移除实际边界来伪造可达。

运行日志出现过NavigationSystem类默认对象相关的handled ensure，发生于Python诊断路径；实际6段走动测试全部成功。不能据此写“零警告”，也不能把诊断提示直接解释为玩家导航失败；保留日志并将具体诊断修正与最终检查结果区分记录。

## 交付证据与测试边界

交付包含真实游戏截图、位置采样轨迹、测试JSON和构建日志，**没有流程录像**，不生成或伪造视频。六区截图用于实际固定镜头观察；站位采样不等于全地图连续跑遍。

当前验证没有穷举整条边界的所有跳跃/翻滚绕行、全部入口异常落点、硬件键盘输入和正式建造占地。有限导航查询、实际3次往返及地形坡度不替代这些系统。源需求中的正式形态、战斗、AI、采集、掉落、背包、建造、破坏门、压力板桥、呼吸开合、气孔近道、坍塌机制及过场仍属对接代理或未实现玩法；资源/角色美术也仍有组合占位。

## 最终保存与归档

地图最终保存后，已于22:55:41启动新的UE进程并重新打开候选关卡。22:57的ResumeEnvironmentAudit再查146个环境对象，挂接/位置/旋转/缩放异常为0；FinalPersistence.json再次通过：1,111个Actor、1,050条布置记录，网格引用、材质、挂接异常均为空，植被6,600，navigation_building=false，保存成功。非交互废弃聚落不在最近资源/建筑焦点数组内，实际数组为8个锚点。

本项目仅排除Maps/DoughWorldDepth/Source/*和Documentation/*的自动资产导入，避免参考图、OBJ与报告被重复导入成资产，其他自动导入设置保留。配置备份和范围见SourceImportExclusions.json。

最终归档已完成：205个源文件、参考源码、说明和真实截图写入Content/Maps/DoughWorldDepth/Source及Documentation，逐文件SHA256复核一致，0错误。详细来源、目标与原生代码一致性见Documentation/ArchiveReport.json。归档过程未修改UAsset/UMap。

