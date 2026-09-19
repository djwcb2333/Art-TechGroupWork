# 玩家跳跃与翻滚：使用和程序对接

更新日期：2026-09-10。引擎：UE 5.8.2。

## 使用

点击运行（Play），再点一下游戏视口，让视口接收键盘输入。

- 空格：在地面上起跳。当前单次跳跃，不支持空中连跳。
- 左 Shift / 右 Shift：沿按下时角色面朝的方向向前翻滚，默认约 4 米。
- 翻滚约持续 0.45 秒；结束后冷却 0.35 秒。持续按住 Shift 不会自动连滚。
- 翻滚期间不能跳跃或再次翻滚。墙面会阻挡并提前结束翻滚，走到边缘开始下落时也会结束翻滚。
- 保留原来的鼠标移动方式。跳跃开始会取消当前寻路；翻滚期间的移动指令在结束时清除，之后可重新点击移动。
- 翻滚没有无敌帧、伤害、耐力或其他战斗机制。

## 蓝图与代码关系

| 项目 | 职责 |
|---|---|
| /Game/TopDown/Blueprints/BP_TopDownGameMode | 继续使用原 BP_TopDownCharacter 和 BP_TopDownController，无需更换游戏模式 |
| /Game/TopDown/Blueprints/BP_TopDownCharacter | 新父类为 /Script/GDATtest.TopDownActionCharacter，原角色模型、偏移与蓝图组件保留；动作参数在此调整 |
| /Game/TopDown/Blueprints/BP_TopDownController | 保留原输入和鼠标移动蓝图 |
| Source/GDATtest/TopDownActionCharacter.h、.cpp | 键盘绑定、跳跃限制、翻滚位移、碰撞处理及姿态恢复 |

这里采用蓝图配置 + C++ 行为实现。修改参数不需要重新编译 C++；修改动作算法或绑定键则需要编译代码。

## 参数位置

打开 BP_TopDownCharacter → Class Defaults（类默认值），搜索 Player Actions 或下面的参数名称。

| 参数 | 默认值 | 意义 |
|---|---:|---|
| Jump Launch Speed | 600 cm/s | 起跳速度；跳跃高度还受 CharacterMovement 的 Gravity Scale 影响 |
| Roll Distance | 400 cm | 无障碍时的目标翻滚距离，实际距离会受帧步长、地面与障碍影响 |
| Roll Duration | 0.45 s | 翻滚持续时间 |
| Roll Cooldown | 0.35 s | 翻滚结束后的等待时间 |
| Is Rolling | 运行时只读 | 动画、音效、UI 后续可读取的状态 |

蓝图可调用 Start Action Jump、Start Forward Roll。它们沿用相同的地面、翻滚状态和冷却检查。

## 实现说明

- 使用 Character 原生 Jump/StopJumping；解除模板的平面限制以允许 Z 轴运动，JumpMaxCount=1。
- 使用 CharacterMovement 的 RootMotionSource ConstantForce 产生前向位移，由胶囊和 CharacterMovement 处理碰撞与地面，不是瞬移。
- 当前翻滚外观是模型整体前翻的占位表现，并非正式的蜷身翻滚动画。胶囊和镜头保持直立。后续可替换为适配 Manny 的翻滚 Montage，并去掉当前整体旋转表现，避免叠加两套动画。
- 当前按单机原型接入；没有实现多人游戏的动作请求 RPC 或专门的预测同步逻辑。

## 验证

- Development Editor C++ 编译成功；角色蓝图编译和保存成功。
- 原角色模型为 SKM_Manny_Simple；重父类前后比较模型资源路径、位置和旋转数值，确认相同。
- 当前 ArtPass 地图运行时的 GameMode 为 BP_TopDownGameMode，玩家为 BP_TopDownCharacter。
- 运行记录：单次跳跃最高离地约 122.40 cm，正常落回地面，JumpCurrentCount 从 1 恢复为 0。
- 两次前向翻滚记录：410.08 cm / 412.68 cm；结束后姿态恢复，水平速度归零。
- 对现有 POI_00_Base 做碰撞测试：翻滚 280.29 cm 后停止，胶囊与实际墙面约有 1.02 cm 间隙，移动输入恢复，翻滚期间的重复翻滚和跳跃未生效。
- 碰撞测试只临时改变 PIE 中的玩家位置，并已还原；没有保存测试位置到地图。

## 备份与记录

原三个 TopDown 蓝图的文件备份位于工作区 UE_Whitebox_Delivery/TopDownMobility/Backup。
另存的中间恢复副本位于 /Game/Maps/CrumbFrontier/Backups/BP_TopDownCharacter_ParentChangeRecovery，不被游戏模式引用。

同目录有 TopDownMobility_Setup.json、TopDownMobility_Collision.json 和运行记录。恢复原蓝图时，应关闭编辑器并从备份恢复；不要在编辑器加载资源期间直接覆盖文件。
