# 面团世界：玩法原型使用与 UI / 音频接入

本文根据本轮 C++ 实现、数据字段和实际运行证据编写，用于玩家试用、数值调整、美术替换和程序接入。

本轮已有实际验证记录：[BuildFinal.log](<E:/GDAT/第一学期/AP6402 Art-Tech Collaboration/Codex Mission/UE_Whitebox_Delivery/FinalGameplay_20260915/BuildFinal.log>) 的 `Result: Succeeded`；[Automation2.log](<E:/GDAT/第一学期/AP6402 Art-Tech Collaboration/Codex Mission/UE_Whitebox_Delivery/FinalGameplay_20260915/Automation2.log>) 中 13 项自动化测试全部成功；[RuntimeValidation.json](<E:/GDAT/第一学期/AP6402 Art-Tech Collaboration/Codex Mission/UE_Whitebox_Delivery/FinalGameplay_20260915/RuntimeValidation.json>) 为 `complete=true`、`passed=true`，61 项运行断言全部通过且 `errors=[]`。运行检查覆盖移动、疾跑累计与耗血、冲刺碰撞、采集、变身、合成、酒精战斗、敌人 / 窝点、保存加载和死亡动画。

[HeroVisualFix.json](<E:/GDAT/第一学期/AP6402 Art-Tech Collaboration/Codex Mission/UE_Whitebox_Delivery/FinalGameplay_20260915/HeroVisualFix.json>) 记录角色蓝图朝向和材质使用标记均已保存；[UI_Crafting_Final00000.png](<E:/GDAT/第一学期/AP6402 Art-Tech Collaboration/Codex Mission/UE_Whitebox_Delivery/FinalGameplay_20260915/Screenshots/UI_Crafting_Final00000.png>) 已进行目视检查，可清楚看到真实 UMG Widget Blueprint 的两条配方和两个各 40 个面粉的堆叠槽。

这些结果属于当前 Windows / UE 5.8.2 开发环境的验证快照，不代表全机型兼容、最终美术完成或已打包 EXE 测试。后续标题相机补丁的构建与画面检查另行记录，不并入本快照。

本轮使用一张独立的平面测试地图，不承担最终场景美术搭建。原 `Content/Programming` 与程序组原有 UI 保留，新玩法使用原 TopDown 行动角色的派生实现。

## 1. 从哪里开始

在内容浏览器打开：

`/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_GameplayPrototype`

本轮目录约定：

| 内容 | UE 资源路径 / 原生类 |
|---|---|
| 测试地图 | `/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_GameplayPrototype` |
| 共享数值与表引用 | `/Game/DoughWorld/Maps/Gameplay/Data/DA_DoughWorldGameplay` |
| 物品定义表 | `/Game/DoughWorld/Maps/Gameplay/Data/DT_DWItems` |
| 合成配方表 | `/Game/DoughWorld/Maps/Gameplay/Data/DT_DWRecipes` |
| 玩家实现 | `ADWPlayerCharacter`，继承 `ATopDownActionCharacter` |
| 玩家控制器 | `ADWPlayerController` |
| 游戏界面 | `ADWGameplayHUD` + `WBP_DWGameplay`（原生父类 `UDWGameplayWidget`） |
| 存档与启动流程 | `UDWGameInstance` |

首次运行、尚未选择存档时显示标题。选择“开始游戏”后进入三个存档槽；在空槽点击“新建”，或对已有槽点击“加载”。标题与游玩使用同一张测试地图，由 `UDWGameInstance::HasActiveSlot()` 区分当前状态。

## 2. 最终按键

| 按键 | 功能 |
|---|---|
| W / A / S / D | 相对当前镜头方向移动；斜向输入不会额外加速 |
| 按住左 / 右 Shift，并移动 | 持续疾跑；实际跑动时才扣生命和累计酿造进度 |
| 空格 | 朝角色当前朝向短距离冲刺，沿用原翻滚的位移机制；当前不绑定跳跃 |
| 按住鼠标右键并拖动 | 调整镜头朝向与俯仰，俯仰有上下限 |
| 鼠标左键 | 朝鼠标落点投掷一份酒精；不再点击地面移动 |
| 按住 F | 采集附近最近的可用资源点；屏幕下方显示物品名与进度 |
| E | 变身值满后进入酵母形态；耗尽后自动回到面团形态 |
| B | 打开 / 关闭背包 |
| Tab | 打开 / 关闭制作界面，界面同时显示背包与配方 |
| Esc | 游戏中打开暂停菜单；背包 / 制作中关闭面板；设置中返回上一页 |

打开背包或制作界面后，玩家移动、攻击、采集和拖动视角被阻断，但世界继续运行，敌人仍可能造成伤害。暂停菜单、标题和设置会暂停世界。死亡界面阻断操作但不暂停世界，以便死亡动画继续播放。

## 3. 界面怎么看

左上角依次显示生命值、变身值和面团 / 酵母状态。右上角环形条显示疾跑产出酒精的累计进度，环下显示当前酒精数量。停止疾跑不会清零该环；未完成的进度会随存档保存。

接近资源点时，屏幕底部中央显示“按住 F 采集……”以及当前采集百分比。松开 F、离开范围或切换采集对象，会重置本次资源采集进度；这与保留进度的疾跑酿造是两套计时。

生命为零后显示死亡界面，可以加载当前存档或返回标题。死亡状态不能覆盖已有存档。

## 4. 采集、背包与使用

资源成功加入背包后，才扣减资源点剩余数量。背包满时不吞掉资源；腾出空间后可以继续获取。

| 资源 | 物品 ID | 当前默认采集规则 |
|---|---|---|
| 水 | `Water` | 无限资源，按住 F 每 1 秒得到 1 个 |
| 酵母 | `Yeast` | 无限资源，按住 F 每 3 秒得到 5 个；每个酵母增加 1% 变身值 |
| 面团 | `Dough` | 有限资源，默认 3 秒获得 3 个，默认总量 3 个 |
| 面粉 | `Flour` | 有限资源，默认 3 秒获得 2 个，默认总量 20 个 |
| 酒精 | `Alcohol` | 由疾跑累计或合成获得，投掷时消耗 |

这些资源的实际规则由各资源 Actor 蓝图 / 实例属性控制。通用父类为 `ADWResourceNode`，水与酵母分别使用 `ADWWaterResourceNode`、`ADWYeastResourceNode` 派生蓝图。可编辑字段包括：

`ItemId`、`HarvestInterval`、`HarvestAmount`、`bInfinite`、`RemainingAmount`、`InteractionRadius`、`bHideWhenDepleted`、`PersistentId`。

背包默认 24 个槽位，每槽最多 40 个，同种物品自动堆叠，超过单槽上限会使用其他空槽。修改 `DA_DoughWorldGameplay` 的 `MaxInventorySlots` 和 `MaxStackSize` 可调整容量。

在背包中点击物品槽，再点击“使用一个”。当前默认只有面团可直接使用，每个恢复 10 点生命；满生命时不会消耗。其他默认物品作为合成 / 战斗材料使用。物品是否可用及效果来自物品表的 `bUsable`、`HealAmount`、`TransformationGainPercent`。

原程序组的 `Wood` 数值没有接入新玩家。本轮物品统一采用 `Dough` 等 ID，通过 `UDWInventoryComponent` 管理数量和堆叠。

## 5. 合成与变身

默认两条配方均要求酵母形态：

| 配方 ID | 材料 | 产物 |
|---|---|---|
| `CraftDough` | `Flour × 2` + `Water × 2` | `Dough × 1` |
| `CraftAlcohol` | `Dough × 3` + `Yeast × 2` | `Alcohol × 1` |

按 Tab 打开制作页。左侧可以浏览背包和使用物品，右侧显示所需材料、当前拥有数量、产出与形态条件。“制作一份”只有在材料、形态和背包空间均满足时可用；制作失败不会部分扣除材料。

变身值达到 100% 后按 E 进入酵母形态，不会在进入时一次性扣空变身值。已确认采用的当前规则是：两种形态均每 5 秒损失 3% 变身值；酵母形态降至 0 时自动回到面团。酵母形态中再次按 E 不会主动切回。

采集酵母和酒精攻击造成命中都能积累变身值。当前酒精区域默认 `bGrantTransformationOnEveryDamageTick=true`，即按有效伤害 tick 触发攻击积累；关闭后，每一份酒精区域最多触发一次积累。`AttackGainPercent` 默认每次 2%。

## 6. 疾跑、冲刺与酒精攻击

`DA_DoughWorldGameplay` 当前默认数值：

| 属性 | 默认值 | 含义 |
|---|---:|---|
| `MaxHealth` | 100 | 生命上限 |
| `BaseMoveSpeed` | 600 cm/s | 默认移速 |
| `SprintSpeedMultiplier` | 1.3 | 持续疾跑的速度倍率 |
| `SprintHealthCostPercentPerSecond` | 2 | 每秒扣除生命上限的百分比；默认每秒扣 2 点 |
| `SprintAlcoholInterval` | 3 秒 | 累计实际疾跑时间达到此值，产出一次酒精 |
| `SprintAlcoholAmount` | 1 | 每次产出的酒精数量 |
| `MaxTransformation` | 100 | 变身值上限 |
| `YeastGainPercentPerItem` | 1 | 每个采集到的酵母增加的变身百分比 |
| `AttackGainPercent` | 2 | 有效攻击积累的变身百分比 |
| `TransformationDecayInterval` | 5 秒 | 变身值衰减间隔 |
| `TransformationDecayPercent` | 3 | 每次衰减占变身上限的百分比 |

冲刺与持续疾跑是独立动作。冲刺距离、时长、冷却仍使用继承自 TopDown 行动角色的 `RollDistance`、`RollDuration`、`RollCooldown` 属性，表现上使用平移 / 冲刺。玩家保留 `DashAnimation`、`DashNiagara`、`DashTrailClass` 特效替换入口。 **当前导入的主角资源没有独立 Dash 动画，因此短冲刺使用平移表现，未宣称已经制作专用冲刺动画。**

投掷消耗 1 份酒精。玩家侧可编辑 `ThrowCooldown`（默认 0.65 秒）、`MaxThrowRange`（默认 1600 cm）、`ThrowOriginOffset` 与 `AlcoholProjectileClass`。酒精区域 `ADWAlcoholArea` 可编辑：

| 属性 | 默认值 |
|---|---:|
| `Duration` | 5 秒 |
| `DamageInterval` | 0.3 秒 |
| `DamagePerTick` | 10 |
| `SlowPercent` | 30% |
| `Radius` | 220 cm |

区域视觉与音效入口为 `AreaVFX`、`AreaSound`。敌人及窝点使用各自蓝图中的 `MaxHealth`，默认分别为 100 和 500。窝点 `AggroRadius` 控制玩家触发范围，`SpawnInterval` 默认 10 秒；范围组件用于编辑时查看，游玩时隐藏。

### 主角模型朝向与材质

`BP_DWTopDownCharacter` 的角色 Mesh 相对旋转应保持 **Pitch = 0°、Yaw = -90°、Roll = 0°**。这是骨骼网格相对胶囊体的朝向，不是摄像机旋转；不要把模型设为 Pitch 90° 来修正镜头。

主角材质 `M_DoughHero` 必须启用 **Used with Skeletal Mesh**。本轮该标记与上述站姿已实际保存，运行检查也确认主角直立、骨骼材质有效。更换材质后应再次检查此使用标记；静态模型可用的材质未必自动适用于骨骼角色。

## 7. 三个存档槽与退出

“开始游戏”页固定提供三个槽位。空槽可以新建，已有槽可以加载或删除。删除需要再次点击“确认删除”，也可以取消；新建不会自动覆盖已有槽。

游戏中按 Esc：

- “保存游戏”：写入当前激活槽。
- “保存并返回标题”：保存成功后返回；保存失败则留在当前游戏并显示原因。
- “返回标题（不保存）”：丢弃自上次保存后的进展，返回标题。
- “继续游戏”：关闭暂停菜单。

死亡后只提供恢复或返回，不允许将零生命状态覆盖进现有存档。存档记录玩家位置、生命、变身值、形态、背包、疾跑累计进度、变身衰减计时，以及代码明确记录的资源 / 窝点状态；它不是整个关卡对象的自动快照，敌人实时位置和行为树运行状态不按完整世界快照持久化。

本地槽名为 `DoughWorld_Final_Slot_1`、`DoughWorld_Final_Slot_2`、`DoughWorld_Final_Slot_3`。开发环境一般位于项目 `Saved/SaveGames`；打包后由 UE 的平台用户存档目录管理。

标题界面提供“退出游戏”。在编辑器中执行退出的效果与独立游戏进程不同，最终退出体验应在 Standalone 或打包版本核验。

## 8. 如何更换每个物品的 UI 图标

1. 将美术图片导入 `Content/DoughWorld/Maps/Gameplay` 下自己的 UI 子目录。
2. 打开 `Data/DT_DWItems`。
3. 选择 `Flour`、`Water`、`Dough`、`Yeast`、`Alcohol` 对应行。
4. 把导入的 `Texture2D` 填到该行 `Icon`，按需要调整 `DisplayName`。
5. 保存表后停止并重新启动游戏，让配置重新读取表。

`Icon` 有贴图时，物品槽直接使用该贴图；为空时才用 `IconColor` 色块与名称占位。每种物品都有独立入口，没有把图片路径写死在 UI 代码里。不要仅为换图修改 `ItemId`，因为资源点、配方、投掷逻辑和存档都用 ID 关联物品。

`DA_DoughWorldGameplay.ItemTable` 应引用 `DT_DWItems`，`RecipeTable` 应引用 `DT_DWRecipes`。启动时 `RefreshDefinitionsFromTables()` 将表读入 `Items` / `Recipes`；引用数据表后，应优先修改表，不要只修改将被表覆盖的数组。新配方使用 `FDWRecipeDefinition` 行结构，编辑 `RecipeId`、`DisplayName`、`Inputs`、`Outputs`、`bRequiresYeast`；投入与产物中的 `ItemId` 必须存在于物品表。

## 9. 在 UMG Designer 中直接编辑界面

本轮界面采用 **C++ 数据与操作逻辑 + 真实 Widget Blueprint 布局**。主界面、物品槽、配方行和存档行都有实际的 UMG WidgetTree，可在 Designer 内选择控件、拖动位置、调整锚点、替换图片、编辑字体及增加动画。它们不是在空 WBP 内包裹一整张 Slate 界面。

资源均位于 `/Game/DoughWorld/Maps/Gameplay/UI`：

| Widget Blueprint | 原生父类 | 编辑内容 |
|---|---|---|
| `WBP_DWGameplay` | `UDWGameplayWidget` | HUD、标题、三槽选择、背包、制作、暂停、设置、死亡页的完整控件树 |
| `WBP_DWInventorySlot` | `UDWInventorySlotWidget` | 物品格、图标、名称、数量、选中外观 |
| `WBP_DWRecipeEntry` | `UDWRecipeEntryWidget` | 配方标题、材料、产物、条件、制作按钮 |
| `WBP_DWSaveSlot` | `UDWSaveSlotWidget` | 存档名、时间、加载、新建、删除确认及取消 |

主 WBP 的 Class Defaults 中有 `InventorySlotClass`、`RecipeEntryClass`、`SaveSlotClass`。需要彻底换一种条目样式时，可复制对应 WBP、调整 Designer 后把新类填入这些字段。`InventoryColumns` 默认 6，可调整背包列数；背包实际槽位总数仍由共享配置控制。Designer 中的列表预览用于看排版，运行时会根据真实物品、配方和存档重新生成条目。

圆环是单独的 `DWProgressRing` UMG 控件，可在 Designer 中拖动、缩放和设置 `Tint`、`Diameter`。它只负责圆弧绘制，不包含整张界面，进度仍由玩家保存的累计计时驱动。

**编辑时保留绑定控件名称与类型。** 例如 `HealthBar`、`TransformationBar`、`SprintRing`、`InventoryGrid`、`RecipeList`、`ItemIcon`、`CraftButton` 等由父类 `BindWidgetOptional` 自动绑定。可以移动、改大小、改字体和画刷；如删除或改名，关联功能会失去该显示 / 按钮入口。`PageSwitcher` 的页面顺序为：标题、存档、背包、制作、暂停、设置、死亡；改顺序时需同步 `ApplyMenuPage()` 的映射。

标题由 HUD 的 `GameTitle`、`GameSubtitle` 提供。以下 HUD 属性作为统一美术覆盖入口继续保留；留空时保留 Designer 自己的样式：

| 属性 | 用途 |
|---|---|
| `WidgetClass` | 整体主 Widget Blueprint，填 `WBP_DWGameplay` |
| `HealthFrameTexture` | 生命条背景 / 边框纹理 |
| `TransformationFrameTexture` | 变身条背景 / 边框纹理 |
| `InventoryPanelTexture` | 背包页背景图 |
| `CraftingPanelTexture` | 制作页背景图 |
| `TitleBackgroundTexture` | 标题背景图 |
| `GameUIFont` | 显式覆盖主树文本字体；留空则按 Designer 字体 |

直接在 Designer 调整的面板背景可以设置九宫格 Margin、图片拉伸和布局。各条目图标仍优先取 `DT_DWItems.Icon`。物品名称与数量、生命值等运行数据会由 C++ 更新；装饰文字和结构直接在 Designer 编辑。

### 蓝图事件：常改表现不必重编 C++

在 `WBP_DWGameplay` 的 Graph 中可以实现：

| 蓝图事件 | 适合扩展 |
|---|---|
| `OnMenuPageChanged(Page)` | 面板开合动画、页面音效、焦点表现 |
| `OnHUDValuesChanged(HealthFraction, TransformationFraction, bYeast, SprintProgress)` | 低血闪烁、变身提示、环形条表现 |
| `OnInventorySelectionChanged(SlotIndex, ItemId)` | 物品说明、额外图鉴、选中动画 |
| `OnItemUseResult(ItemId, bSuccess)` | 使用成功 / 失败表现 |
| `OnRecipeCraftResult(RecipeId, bSuccess)` | 合成成功动画、提示音、产物展示 |
| `OnUIAction(Action)` | Start、Settings、Save、LoadSlot、NewSlot、DeleteSlot、ReturnToTitle、ApplySettings、Quit 等动作后的扩展 |

条目 WBP 另有 `OnItemPresentationUpdated(bSelected)`、`OnRecipePresentationUpdated(bCanCraft)`、`OnSavePresentationUpdated(bConfirmingDelete)`，可在原生赋值之后覆盖选中配色、显示规则和动画。核心扣物、合成验证和存档写入仍由 C++ 执行。需要完全接管主界面按钮流程时，可在主 WBP Class Defaults 关闭 `bBindDefaultButtonActions`，然后在 Graph 中自行连接按钮 OnClicked 并调用现有公开接口；默认开启即可使用完整原型流程。

需要自己布置按钮流程时，可调用这些蓝图接口：

| UI 所需操作 | 接口 |
|---|---|
| 玩家状态 | `GetPlayer()` → `GetHealth()`、`GetTransformation()`、`IsYeastForm()`、`GetSprintAlcoholProgress()` |
| 采集提示 | 玩家 `GetInteractionPrompt()` |
| 读取物品 | `GetInventory()` → `GetInventorySlots()` / `CountItem()` |
| 选择物品 | Widget `SelectInventorySlot(SlotIndex)` |
| 使用物品 | 玩家 `UseInventoryItem(SlotIndex)` |
| 制作 | Widget / 玩家 `CraftRecipe(RecipeId)` |
| 更新通知 | `UDWInventoryComponent.OnChanged` |
| 面板开关 | HUD `ToggleInventory()`、`ToggleCrafting()`、`TogglePause()`、`ClosePanels()` |
| 阻断玩法输入 | HUD `IsBlockingGameplay()` |
| 存档按钮操作 | Widget `LoadSlot()`、`NewSlot()`、`DeleteSlot()`；GI `SaveCurrentGame()` |

原 `Content/Programming` 中的 UI、图标和程序成果继续保留，新 WBP 不覆盖旧 Widget。

### 供程序使用的 Designer 资产生成入口

Editor 中可执行 `unreal.DWUIAuthoringLibrary.create_prototype_ui_assets(False)`，并读取 `get_last_build_report()`。它创建并保存上述四个真实 Widget Blueprint；默认保留已经存在的 Designer 资产。`True` 会重新构建现有原型控件树，**后续美术已改排版后不要用 True 重建**。这是首次搭建工具，不是每次游戏启动运行的代码；非 Editor 构建会返回 false。

## 10. 音效和背景音乐接入

打开 `Data/DA_DoughWorldGameplay` 的 `Audio` 分类：

| 属性 | 填入内容 |
|---|---|
| `UIClickSound` | 点击按钮、选择物品槽和打开 / 关闭面板的声音，类型 `USoundBase` |
| `MenuBGM` | 标题、存档选择等开始流程使用的音乐 |
| `GameplayBGM` | 进入存档游玩后的音乐 |
| `MusicVolume` | BGM 基础音量，默认 0.7 |
| `UIClickVolume` | UI 点击基础音量，默认 0.8 |

先导入声音资源，再拖入对应字段。Sound Wave 和 Sound Cue 等 `USoundBase` 资源均可赋值。字段为空时静默运行；无需为了占位制作空音频。UI 会切换菜单 / 游戏 BGM，销毁旧音频组件，并在曲目结束后重新播放；暂停时 BGM 按 UI 音频继续播放。

变身音效与镜头晃动在玩家蓝图上单独配置：`TransformSound`、`TransformCameraShake`。没有自定义 CameraShake 类时，代码仍保留基础镜头晃动参数 `TransformShakeSeconds`、`TransformShakeMagnitude`、`TransformShakeFrequency`。

酒精区域另有 `AreaSound`；冲刺效果入口为 `DashNiagara`、`DashTrailClass`。后续从 Fab 或自行制作得到素材后，把资源分配给这些字段即可。

## 11. 设置

设置页包含主音量、分辨率、窗口 / 无边框 / 全屏、画质等级和垂直同步。音量拖动立即生效；点击“应用并保存”保存设置。

图像设置使用 UE `UGameUserSettings`。主音量当前通过音频设备的主音量应用，保存到 `GameUserSettings.ini` 的 `[DoughWorld.UserSettings]`、`MasterVolume`。它与每个音效 / BGM 的基础音量相乘。

分辨率按钮提供 1280×720、1600×900、1920×1080、2560×1440。嵌入编辑器的 PIE 窗口不能完全代表全屏 / 窗口模式的最终效果，应在独立运行窗口验证显示设置。

## 12. 当前原型边界与交接建议

- 本轮是单张平面玩法测试图，不是最终关卡美术、镜头演出或环境搭建成果。
- 新玩家、采集、制作、背包、存档、敌人与 UI 通过新玩法类协作；原地图和 Programming 成果保留。
- 所有页面布局已转为可编辑 UMG Blueprint；图标、面板、音效和 BGM 仍需美术替换，占位图形与音频留空不能视为最终美术完成。
- 新加入物品或修改配方后，应测试满背包、材料不足、非酵母形态和读旧存档。修改既有物品 ID / 大幅缩减容量可能使旧存档验证失败，应保留兼容规则或使用新的测试槽。
- 新添加有限资源点或窝点时，给 `PersistentId` 配置稳定且不重复的 ID，便于存档识别。
- 已完成的 C++ 构建、13 项自动化测试、61 项运行断言和制作页目视检查见文首证据。不同显示设备、其他机器、完整窗口模式矩阵及打包 EXE 的测试结果不在本次证据范围内。
