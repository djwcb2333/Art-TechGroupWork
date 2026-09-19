# 固定镜头的局部遮挡渐隐

## 1. 用户已确认

固定方位、不可缩放。遮挡时让挡住角色的局部环境部件渐隐，类似用户提到的《夜族崛起》的表现方向。这里提供独立的UE实现候选，不声称与参考游戏算法相同。

## 2. 本包提供的代码

Code/CameraOccluderFadeComponent.h与.cpp是可交给搭建Codex接入的组件候选，**尚未在GDATtest编译或运行**。复制至实际模块Source/GDATtest的合适目录，在正确引擎版本编译并解决真实编译问题后使用；不要用Live Coding输出或文件存在当成运行成功。

组件加在需要渐隐的环境Actor上。给其需渐隐的StaticMeshComponent添加组件标签`CameraFade`。标签不是ActorTag。树冠、菌盖、屋顶、前景墙上半段需拆成独立组件；树干、门、台缘等不贴标签。

代码只为单个本地玩家提供初版逻辑。多人或分屏需要每视图材质策略，不能共享一个全局淡出状态。本次不擅自加入多人系统。

## 3. 判定与渐变

代码读取实际PlayerCameraManager与实际Pawn，不改变位置、旋转、FOV、OrthoWidth或SpringArm长度。以头部、身体两侧和下半身四个目标检测遮挡：透视使用相机到目标的线段，正交使用平行方向线段。

每个标记组件独立检测其世界AABB，因此多个叠挡对象都能被处理，不会只找到第一堵墙。AABB是保守的视觉代理，可能提前淡化或在不规则大模型空隙处误判；需要把大模型拆小或调整边界。它不依赖树冠拥有阻挡玩家的碰撞，适合独立无碰撞树冠。

默认约0.08秒检查一次遮挡；0.18秒淡出到20%可见；离开判定后保留0.2秒，再用0.3秒恢复。都是候选调参。保留短延迟是为了避免玩家在墙沿移动时闪烁。各材质槽的原始参数值会被保留，组件结束时尽量恢复自己替换的材质；不覆盖其他系统后来换上的材质。

## 4. 材质必须配合

标量参数`CameraFade`默认1，0代表不可见，1代表原外观。代码发现槽位缺少参数时输出警告并跳过，不能假装淡出完成。

候选接线：保持原BaseColor/Normal/Roughness；采用Masked材质，将原有叶片/孔洞Alpha与`DitherTemporalAA(CameraFade)`相乘接到Opacity Mask。原来没有Alpha的实体材质可把原Alpha视为1。这样不会破坏树叶原有镂空。实际项目的AA模式、Nanite与材质路径必须验证；出现噪点或拖影时先调整实现，不能直接把所有场景材质换成Translucent。

如果选择真正Translucent材质，应复查排序、阴影和重叠表现。无论哪种方式，视觉渐隐不修改碰撞、导航或交互判定。

仅存在参数不证明参数接到了可见性输出；必须在PIE手动把一个材质的CameraFade设为0.2，确认实际画面变化后，再测试组件自动控制。

## 5. 实例化植被

代码明确跳过ISM/HISM，避免给共享组件换一个MID导致整片树林变淡。低矮草簇无需渐隐，可继续实例化。可能遮挡角色的主树冠和巨菇菌盖，首轮用独立StaticMeshComponent接入上述组件。

如果后续确实需要实例化高大植被的逐实例渐隐，新增PerInstanceCustomData通道并根据实例ID写入淡出值；材质读取这个通道。不能直接把现有共享MID方案用于成千上万实例。该分支本包未实现，必须如实记录。

## 6. 专项验收

| 编号 | 操作 | 通过条件 |
|---|---|---|
| O01 | 走到树冠背后，再走出 | 树冠渐隐/恢复，树干不消失，角色轮廓可读 |
| O02 | 穿过菌盖与墙片同时遮挡的区域 | 两个遮挡组件都处理；不会只淡第一层 |
| O03 | 只遮挡同资产的其中一个Actor | 其他同资产Actor保持原样 |
| O04 | 沿墙沿来回小幅移动 | 无明显开关式闪烁 |
| O05 | 阻挡墙淡出后尝试穿过 | 碰撞仍生效；不是穿墙功能 |
| O06 | 屋顶挡住基地建筑操作 | 屋顶淡化，地板、门口与交互仍可辨认 |
| O07 | 死亡、重生、切图、停止PIE | 材质恢复，无残留半透明状态或失效引用 |
| O08 | 检查材质缺参、ISM误贴标签 | 明确日志；记录未接入对象，不算完成 |
| O09 | 核对相机前后参数 | 朝向、距离、投影和FOV均未改变；无新增缩放 |
| O10 | 从实际固定相机审视所有关键场景 | 淡出足以露出角色和交互，但未抹除危险地形信息 |

## 7. 官方API依据

- [UMaterialInstanceDynamic](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UMaterialInstanceDynamic)：动态材质参数。
- [Unreal Material Properties](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-material-properties)：Masked等混合模式。
- [SweepMultiByObjectType](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/Engine/UWorld/SweepMultiByObjectType?application_version=5.5)：多次扫掠仍可能止于首个阻挡结果，不能未经处理就当成“所有遮挡物”列表。本组件改用逐组件范围判定，代价是保守误判与需要合理拆分模型。

## 完整包补充边界

代码路径相对包根目录Code/。复制到实际工程模块后再编译，模块名和目录必须以现场为准。当前代码只检测角色采样点，不接受任意交互目标；00/01要求的交互目标可读性还需要接入实际目标或通过环境构图实现。不得把未实现分支报告为已实现。本文所述API依据保留作查阅，仍需在实际引擎版本验证。
