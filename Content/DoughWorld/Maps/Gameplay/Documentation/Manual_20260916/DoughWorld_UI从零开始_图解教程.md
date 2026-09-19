# Dough World UI 从零开始 · 图解教程

这是给第一次使用 UE 界面编辑器的独立学习入口。按下面顺序打开本工程已有资产、做一个小修改，再运行验证；不需要先读完整参数索引。图片来自真实工程，编号框只用于指出操作位置。

## 先选你现在要做的事

| 你现在要做什么 | 从哪里读起 |
|---|---|
| 不知道 Designer、Graph、控件树在哪里 | 第一部分：UI 从零入门，从第 1～3 节开始 |
| 换按钮、Logo、图标或字体 | 第一部分第 5～7、12 节；第二部分第 4、12 节 |
| 做黑幕→Logo→3D 主菜单，改动画时间轴 | 第二部分第 1～9 节；从空白练习则读第 10 节 |
| 让翻页弹出、主菜单背景轻微晃动 | 第二部分第 13、14 节 |
| 让按钮悬停、按住时弹一下 | 第三部分：可挂载的 UI 弹跳组件 |
| 看懂材质、给房屋加入遮挡淡出 | 最后附录：材质与遮挡截图实操 |

第一次练习建议只完成一件小事：打开现有 WBP → 调一个按钮的外观 → 编译和保存 → 让对应界面真实显示并确认变化。随后再练布局、贴图、声音和动画。不要同时改一整页后再猜问题来自哪里。

本教程的第一、二、三部分分别对应完整手册第 6、7、8 章；材质附录对应第 5 章。因此正文保留原来的“第七章/第八章”交叉引用和图号。需要全部绑定控件名、所有音频槽与玩法数据时，打开同目录 [完整操作与修改手册](DoughWorld_完整操作与修改手册.html)，查第 3 章和参数索引。

这是修改教学。项目编译、真实游玩、打包和性能验证以本轮专项记录为准；文中的练习步骤不表示已经替你搭建全部练习页面。运行截图能说明画面，不能单独证明所有行为都已测试。


## 第一部分 · UI 从零入门（完整手册第 6 章）

本章按“第一次打开 UE 的 UI 编辑器”来讲。先用项目里已有的界面认识位置和做小修改，再学习自建界面的通用连接方式。现有工程的全部控件绑定、音效槽和程序接口，以前面的《UI、交互提示、字体、语言与音频修改手册》为准；这里不把教学步骤说成已经替你新建了页面。

### 1. 先知道自己正在编辑什么

- **UMG**：UE 制作游戏界面的系统。
- **Widget**：一个界面元素，可以是文字、图片、按钮，也可以是一组组合好的元素。
- **Widget Blueprint / 控件蓝图，通常以 WBP 开头**：可保存、复用的界面资产。里面有外观布局，也可以有蓝图逻辑。
- **Designer / 设计器**：摆放控件、修改尺寸和皮肤。
- **Graph / 图表**：连接“按钮点击后做什么”“数据变化后怎样更新”等逻辑。
- **HUD**：游戏中管理屏幕界面的对象。本项目已有 HUD 负责创建主 WBP、切换页面、处理输入与声音。

你的现有界面采用“C++ 管理游戏数据和流程，WBP 负责可视化布局，并提供蓝图表现事件”的方案。**换图、排版、字号和现成声音槽通常不用改 C++；增加一个完整游戏状态页或新业务规则可能需要程序接入。**

### 2. 第一次打开本项目 UI

1. 停止游戏。打开内容浏览器，依次进入 `Content → DoughWorld → Maps → Gameplay → UI`。
2. 双击 **WBP_DWGameplay**。不要误开 `BP_DWGameplayHUD`；它是管理界面的 Actor 蓝图，不是拖控件的设计器。
3. 点击右上 **Designer / 设计器**。若只看到节点，当前是在 Graph，切回来即可。
4. 先在左下 Hierarchy / 层级的搜索栏输入 `StartButton`，选中它，观察右侧详情随选择变化。
5. 清空层级搜索后可以重新看到完整控件树。右侧详情的搜索栏也是独立的；如果某个属性找不到，先清空那里的过滤词。

![图6-1：本项目WBP_DWGameplay真实设计器，七个常用位置已编号](assets/MaterialUIBasics/UI01_UMGDesigner_annotated.svg)

图中七个位置分别是：①控件板，用来找新控件；②控件树，用来选中和组织控件；③设计预览；④当前选中控件的详情；⑤外观/逻辑切换；⑥是否变量；⑦动画抽屉。界面布局可以被用户拖动调整，因此以面板名称为准，不必与你屏幕上的像素位置完全一致。官方也按这些区域说明 Widget 编辑器。[Epic：Widget Blueprints](https://dev.epicgames.com/documentation/en-us/unreal-engine/widget-blueprints-in-umg-for-unreal-engine)

#### 2.1 先记住这些真实入口

以下都在 `/Game/DoughWorld/Maps/Gameplay/` 下：

| 你想改的内容 | 打开的资产 |
|---|---|
| 游戏内 HUD、背包、制作、暂停，以及旧标题/设置页面 | `UI/WBP_DWGameplay` |
| 本轮新增独立开场与主菜单、它自己的存档/设置页面 | `Frontend/UI/WBP_DWFrontend`，具体步骤见第七章 |
| 一个背包格子的外观 | `UI/WBP_DWInventorySlot` |
| 一条合成配方的外观 | `UI/WBP_DWRecipeEntry` |
| 一个存档条目的外观 | `UI/WBP_DWSaveSlot` |
| UI 总字体、标题文字、部分图片接口 | `Blueprints/BP_DWGameplayHUD` 的 Class Defaults |
| 每件物品的图标和中英文名称 | `Data/DT_DWItems` |
| 合成内容和配方名称 | `Data/DT_DWRecipes` |
| 点击、打开界面、合成声音和 BGM | `Data/DA_DoughWorldGameplay` |
| 物体上方采集提示的布局 | `UI/Interaction/WBP_DWInteractionPrompt` |

注意现在有两份主界面资产：游玩中的 UI 继续由 `WBP_DWGameplay` 负责，新展示关卡的开场和标题菜单由 `WBP_DWFrontend` 负责。要改当前启动时看到的主菜单，应打开后者；只改旧 WBP 不会自动改变新 Frontend。新菜单的 HUD 类默认值入口是 `Frontend/Blueprints/BP_DWFrontendHUD`。不要因为名字接近而同时修改两份再猜哪份生效。

#### 2.2 学习时怎样保留原版

在内容浏览器右键 WBP → Duplicate / 复制，可得到一个练习副本。复制保留原来的 C++ 父类，比新建一个无关父类的 WBP 再硬换上去更适合练习换皮。

**改副本不会自动改变游戏。** 只有现有 HUD 或主界面的模板引用指向它，游戏才会使用；具体替换入口见第 3 章。只想先学 Designer，可以在副本中练习，不急着替换正式引用。不要运行旧的“重新创建全部默认 UI”脚本来保存界面，它可能重建并覆盖手工布局。

### 3. 控件树与容器：拖进去之前先选对父级

Hierarchy / 层级是一棵树，缩进表示“谁装在谁里面”。点击小三角展开；拖控件到指定父级下面改变关系。拖放前看清高亮目标，避免不小心把整页移进一个按钮。

**控件本身和它占用的 Slot 是两件事。** 控件决定“它是什么”，Slot 决定“父容器怎样安排它”。所以同一个 Button 放进 Canvas 后有锚点，放进 Vertical Box 后则主要有 Padding、Size 和对齐；不是属性丢了。[Epic：UMG Slots](https://dev.epicgames.com/documentation/unreal-engine/umg-slots-in-unreal-engine)

| 容器 | 适合用途 | 选中子控件后优先看什么 |
|---|---|---|
| Canvas Panel | 屏幕角落状态区、居中弹窗等外层定位 | Anchors、Position/Offsets、Size、Alignment、Z Order |
| Vertical Box | 从上到下排列标题、按钮或配方 | Padding、Auto/Fill、高宽方向对齐 |
| Horizontal Box | 从左到右排列图标、名称、数量 | Padding、Auto/Fill、对齐 |
| Overlay | 底图、文字、角标叠放 | 子项顺序、Padding、对齐 |
| Size Box | 给一个内容指定宽高或范围 | Width/Height Override 是否勾选及对应数值 |
| Border | 一张底板包着一组内容 | Brush、Padding；它本身只装一个直接子控件 |
| Scroll Box | 内容超过面板时滚动 | 外层尺寸是否受约束，滚动方向 |
| Uniform Grid Panel | 背包格子 | 行、列和单元格布局 |
| Widget Switcher | 同一位置一次显示其中一页 | 当前 Active Widget Index 与页面顺序 |

Button、Border、Size Box 这类单内容控件若要放图标加文字，先放一个 Horizontal Box 或 Overlay，再把多个控件放进那个容器，不能硬把多个独立子控件塞给它。

#### 3.1 跟着做：调整已有按钮间距

1. 打开 `WBP_DWGameplay`，在层级选中 `StartButton`。
2. 右侧最上方的 Slot 显示 **Vertical Box Slot**，说明它由竖排容器安排位置。
3. 展开 Padding，分别观察上、下间距；先只改一边，看看预览中按钮与相邻按钮之间是否变化。
4. 想让按钮占满可用宽度，检查 Horizontal Alignment 是否为 Fill。想改按钮组整体位置，应该选它的外层容器，而不是在按钮上找不存在的 Canvas X/Y。
5. 编译、保存，运行后确认按钮仍能点击。若只是在副本上练习，预览变化即可，不会自动替换正式界面。

不要把 `PageSwitcher` 的页面随意上下拖动。现有索引固定为：0 标题、1 存档、2 背包、3 制作、4 暂停、5 设置、6 死亡；交换顺序会让原有按钮打开错误页面。

### 4. 锚点、对齐、尺寸：解决换分辨率就跑偏

**Anchor / 锚点**决定相对父 Canvas 的哪个位置定位；**Alignment / 对齐枢轴**决定用控件自身的哪个位置贴到该锚点；Position/Offsets 是与锚点之间的偏移。位置单位是 UI 的 Slate 单位，不能直接当作场景厘米；屏幕上的结果还受 DPI 缩放影响。[Epic：UMG Anchors](https://dev.epicgames.com/documentation/en-us/unreal-engine/umg-anchors-in-unreal-engine-ui)

#### 4.1 做一个始终居中的面板

1. 先选中**直接放在 Canvas 下的外层面板**，确认右侧是 Canvas Panel Slot。
2. 把 Anchors 设为屏幕中央：Min 和 Max 都是 `(0.5, 0.5)`。
3. 把 Alignment 设为 `(0.5, 0.5)`。否则控件的左上角可能贴着中心，看上去就偏到右下方。
4. 位置偏移先设为 0，再通过 Size 或外层 Size Box 设置想要的宽高。
5. 在 Designer 的屏幕尺寸/预览设置切换 1280×720、1920×1080，再检查一个较窄比例；进入游戏拉动窗口确认。

#### 4.2 做铺满屏幕的背景

1. 将背景 Image 放在合适的外层 Canvas 下，作为按钮面板的下层。
2. Anchors 设为 Min `(0,0)`、Max `(1,1)`，四边偏移先设为 0。拉伸锚点下应理解成边距，而不是仍用固定宽高的思路。
3. 把背景设置为不接收鼠标命中，避免它遮住按钮点击。
4. 检查图片比例：铺满可能拉伸图片；要保比例，可用 Scale Box/合适容器处理，或者让美术准备适合宽屏裁切的背景。

#### 4.3 为什么 Size Box 设了大小还不对

1. 检查 Width Override / Height Override 左边是否勾选；只输入未启用的数值可能不生效。
2. 看父容器给它多少空间，Auto/Fill 是否和预期一致。
3. 检查子 Image 的对齐与图片尺寸。设置 Desired Size 不等于强行突破父级限制。
4. 检查是否用了 Render Transform 缩放。它主要改变绘制表现，不会让周围控件自动按新体积重新排版；正常布局先用容器和 Slot。

### 5. Image、Text、Button：三种最常用控件

#### 5.1 Image：放贴图，不等于可点击按钮

1. 从 Palette 搜索 Image，拖到你选好的父容器。现有主界面换图优先使用已有 Image，不必新盖一层。
2. 选中 Image，在 Details 展开 Brush，给 Image/资源指定 Texture。
3. 将 Color and Opacity 设为白色、Alpha 为 1，先看原图。这里的颜色会与贴图相乘；黑色会把图染黑。
4. 通过外层 Size Box 或合适 Slot 定大小。透明像素仍属于图片矩形范围，图像文件留了很宽的透明边，视觉内容就会显得小。
5. 仅作装饰时，Visibility 选择不参与命中。需要点击效果时通常用 Button 包住图，而不是期待 Image 自动获得 OnClicked。

#### 5.2 Text Block：预览文字与游戏文字分开看

1. 选中 Text Block，在 Content → Text 改预览文字。
2. 在 Font 调字号，在 Color and Opacity 调颜色；需要可读性可加适量 Outline。
3. 长段文字勾选 Auto Wrap Text 或设置明确换行宽度，并给容器足够高度。
4. 进入游戏后如果文字又变回去，先检查它是否由 C++ 或数据表写入，不要反复修改同一个预览字段。

本项目 `HealthValueText`、`TransformationValueText`、`QuantityText` 都会被实际数据刷新。标题来自 `BP_DWGameplayHUD.GameTitle` / `GameSubtitle`；物品名称来自数据表。改变显示文本不等于改变血量、物品数量或标题数据源。

#### 5.3 Button：皮肤与行为分别处理

1. 在层级中选中 **Button 自身**，而不是它里面的 Text。
2. 在 Style 分别设置 **Normal、Hovered、Pressed、Disabled**。只改 Normal 时，鼠标放上去可能仍显示旧皮肤。
3. 调整 Content Padding，让文字或图标离边框合适；再检查按钮内部 Text 的字体和对齐。
4. 边框图需要伸缩但不想拉坏四角时，可考虑 Brush 的 Box 绘制方式与 Margin；Margin 按原图比例定义，不是直接填像素。先小范围调整并对照原图。
5. 测试四个状态，尤其是不可制作时的 Disabled。

现有背包 `SlotButton` 的选中/未选中背景色会被 C++ 刷新。若运行时盖掉你在 Designer 设的颜色，可在条目的 `OnItemPresentationUpdated` 通知里补选中装饰，或让程序改选择颜色逻辑。不要以为换一张 Normal 图就已经改掉所有运行时状态。

### 6. 导入自己的 UI 美术，并真正替换进游戏

#### 6.1 导入前准备

优先准备带透明通道的 PNG，保留可编辑源文件。SVG 可以保留作美术源，但当前项目这些 `Texture2D` 图标槽应使用导出的位图，不要把 SVG 文件名直接填进去。JPG 没有透明通道，不适合需要透明背景的图标。

按钮四态、底板、图标、进度条底图/填充图应按用途分开。把整张含文字的界面截图导成一张 Image，不能自动得到按钮点击、双语文字或动态数值。

#### 6.2 导入与透明检查

1. 在 `/Game/DoughWorld/Maps/Gameplay/UI/` 下建立自己的美术目录，点击 Import / 导入，选择 PNG；或者从资源管理器拖入这个目录。
2. 双击贴图，在纹理编辑器检查 RGB 和 Alpha 通道。**Alpha 白色代表显示，黑色代表透明**；如果 Alpha 是全白，先回绘图软件确认有没有真正导出透明通道。
3. 在 Details 搜索 `Texture Group`，屏幕 UI 贴图可设为 **UI**。颜色图通常保留 sRGB；数据遮罩按数据用途处理，不要把所有贴图一律关闭 sRGB。
4. 在 Compression Settings 保留支持所需颜色/Alpha 的格式，先观察边缘质量。旧教程的 `UserInterface2D (RGBA)` 在本机 UE 5.8 源码中对应的显示名已改为 **Uncompressed (RGBA8)**；它显存开销较高，不要把所有大背景无条件改为无压缩。出现明显压缩块再按用途选择 BC7 或无压缩，并复查大小与显示质量。
5. 保存贴图，在实际 Image/按钮上查看，而不只看纹理窗口。透明边缘发白也可能来自导出时白底残留；仅把 UI 颜色调暗不能修好源图边缘。

上面的压缩名称已核对本机 UE 5.8 `TextureDefines.h`，避免照旧教程寻找不存在的同名选项。引擎会对导入贴图进行类型识别，识别错误时应在贴图资产中校正，特别是被误判为法线图的 UI 图。[Epic：贴图导入与压缩说明](https://dev.epicgames.com/documentation/unreal-engine/importing-content-into-unreal-engine-from-maya)

#### 6.3 给“水”换上真实图标

1. 打开 `Data/DT_DWItems`，选择 `Water` 行。
2. 在 `Icon` 选择刚导入的图标。中文填 `DisplayName`，英文填 `EnglishDisplayName`；**不要改 ItemId 来改显示名**。
3. 保存表。检查 `DA_DoughWorldGameplay.ItemTable` 仍指向这张表。
4. 重新运行，采集水，按当前背包键打开背包，再打开制作页左侧背包。两处都应显示同一图标。
5. 图标需要变大时，打开 `WBP_DWInventorySlot`，调整 `ItemIconSize` / `ItemIcon` 所在布局；不要改 QuantityText 来控制图标大小。

当前 `IconColor` 是缺图占位色；有正式图标时按白色显示，不是用 IconColor 给图标染色。配方行目前主要是文字，没有独立的 Recipe Icon 字段；美术做出了配方图标，也仍需新增 Image 与读取图标的展示逻辑。

#### 6.4 背景和底板为什么不能只在 Designer 随便换

本项目已有以下运行时图片入口：

| 在 `BP_DWGameplayHUD` 类默认值填写 | 实际写入位置 |
|---|---|
| TitleBackgroundTexture | 标题页 `TitleBackgroundImage` |
| InventoryPanelTexture | 背包 `InventoryCard` 底板 |
| CraftingPanelTexture | 制作 `CraftingCard` 底板 |
| HealthFrameTexture | HealthBar 的背景画刷 |
| TransformationFrameTexture | TransformationBar 的背景画刷 |

设置资源后编译保存，重新 Play。背景图片不一定自动铺满全屏，还要按第 4 节调整对应容器；进度条背景贴图也不会自动创建独立外框或改变填充轮廓。

同一张图片更新美术时，可在纹理资产右键 Reimport / 重新导入，保持引用不变；导入一个新名字的文件不会自动把旧引用全换过去。更新后重新检查尺寸、Alpha 和按钮四态。

### 7. 是否变量、Graph 和事件：让按钮真的做一件事

#### 7.1 “Is Variable / 是否变量”是做什么的

在 Designer 选中控件，右侧名称附近勾选 **Is Variable / 是否变量**。编译后，这个控件可以在 Graph 的 My Blueprint 中被引用；拖出来选择 Get，再从引脚找 `Set Text`、`Set Visibility`、`Set Brush from Texture` 等操作。

它不表示“玩家可以输入”，不会自动保存，也不会给 Image 增加点击事件。现有 C++ 绑定控件的名字和类型不要改；完整名单见第 3 章，例如 `StartButton` 必须仍是 Button，`ItemIcon` 必须仍是 Image，`HealthBar` 必须仍是 Progress Bar。

#### 7.2 在练习副本里接一个点击反馈

1. 选中一个你自己新加的 Button，确保不是现有业务按钮。
2. 在 Details 找到 **Events / 事件**，点击 **On Clicked** 旁的 `+`；编辑器会切到 Graph，放置该按钮的点击事件。
3. 从白色执行引脚拖线，搜索 **Print String**。在文本里写一句测试消息，编译。
4. 把这个练习 UI 按下一节的方式显示到游戏中，实际点击一次，确认只出现一次反馈。Designer 预览里点按钮不是完整的运行测试。
5. 反馈正确后，再用真实功能节点替换打印。白色连线表示执行顺序；彩色引脚传递数据，不能只放两个节点不连白线就期待它们依次运行。

**本项目现有 Start、Craft、Save 等按钮已经有原生绑定。** 不要又在同一个 OnClicked 里执行一遍新建存档、扣材料或制作，这会双触发。改皮肤保留 `bBindDefaultButtonActions` 开启；要改变既有业务流程，按第 3 章的真实入口与边界处理。

#### 7.3 现有界面优先用这些“通知事件”加表现

在 Graph 的 Overrides 或右键搜索中找事件；它们发生在原有逻辑完成后，不代表要你再做一遍业务：

| 已有事件 | 适合连接 |
|---|---|
| OnMenuPageChanged | 对刚进入的页面播放入场动画 |
| OnInventorySelectionChanged | 改详情装饰、选中框 |
| OnRecipeCraftResult | 合成成功/失败的视觉反馈 |
| OnItemUseResult | 使用物品反馈 |
| OnLanguageChanged | 刷新你新增的双语文本 |
| OnHUDValuesChanged | 更新自定义血量/变身图像，不要无判断地反复播放音效 |

### 8. 创建、显示、关闭 UI：只对你自建的独立界面操作

现有 `WBP_DWGameplay` 由 HUD 创建并管理。**不要在角色 BeginPlay 再 Create 一次相同主界面**，否则会叠两层、重复按钮与声音。背包、制作、暂停用已有 HUD 的 `ToggleInventory`、`ToggleCrafting`、`TogglePause` 或 `ClosePanels` 等入口。

下面是自建独立界面的通用流程，具体示例资产由后续实操章提供：

1. 在内容浏览器选择 **Add → User Interface → Widget Blueprint**。普通练习页可选择 UserWidget 父类；现有项目条目换皮则复制兼容 WBP，保留其原 C++ 父类。
2. 由负责打开它的蓝图执行 **Create Widget**，Class 选自己的 WBP，Owning Player 传本地 Player Controller。
3. 把 Return Value 提升为变量保存起来。再次打开前先检查这份引用是否有效，避免每按一次都创建一份叠在屏幕上。
4. 对这份引用调用 **Add to Viewport**，它才会出现在屏幕上。
5. 关闭时对同一份引用调用 **Remove from Parent**。只把引用改为空不会自动把仍在屏幕上的界面移走。
6. 需要把子条目放进一个列表时，使用容器的 **Add Child**；子条目通常不再单独 Add to Viewport。

这些是显示对象的生命周期，不会自动处理暂停、库存或存档。[Epic：创建和显示 Widget](https://dev.epicgames.com/documentation/unreal-engine/creating-widgets-in-unreal-engine?lang=en-US)

`Construct` 不是永久只发生一次的“游戏开始”：移除后再次加入等情况可能重新构造显示。不要在这里无条件重复绑定委托、创建循环声音或发奖励。自己的循环声音/定时器应在关闭和结束时清理；现有 HUD 已管理自己的这部分生命周期。

### 9. 鼠标、焦点与“按钮按不了”

#### 9.1 三个输入模式的区别

| 输入模式 | 理解方式 | 新手常见问题 |
|---|---|---|
| Game Only | 主要交给游戏控制 | UI 已出现却没有正确进入菜单交互状态 |
| Game and UI | UI 先处理，未处理输入可能继续给游戏 | 点击界面同时触发攻击，需要明确阻止游戏动作 |
| UI Only | 只让 UI 接收输入 | 玩家控制器上的关闭键可能不再收到输入 |

创建 UI 和显示鼠标是两件事。独立菜单通常由 Player Controller 配置 Set Input Mode，并设置 Show Mouse Cursor；焦点应交给合适的 Widget。关闭后应恢复之前的游戏输入状态，不是无条件隐藏鼠标——本项目俯视操作本来就可能需要鼠标指针。[Epic：输入模式与鼠标光标](https://dev.epicgames.com/documentation/unreal-engine/creating-widgets-in-unreal-engine?lang=en-US)

现有 HUD 已处理菜单焦点和输入。不要一边调用 `ToggleInventory`，一边又从另一个蓝图强制设置 UI Only 或抢焦点；优先走现成入口。

#### 9.2 按钮不能点，按这五项检查

1. **点对对象了吗？** OnClicked 要绑在 Button 上，不是里面的 Text 或 Image。
2. **是否被禁用了？** Details 的 Is Enabled 为 false，或原逻辑因材料不足禁用按钮，点击不会执行。
3. **上面是否盖着东西？** 一张全屏透明 Image 即使看不见，也可能挡住鼠标。装饰图用 `Not Hit-Testable (Self & All Children)`；装饰容器内部还有按钮时用仅 Self 不参与命中的模式。
4. **父层是否把孩子也禁用了？** 把整页设为 Self & All Children 不可命中，会连按钮一起关掉。本项目 `HUDLayer` 是显示层，添加交互按钮应放到正确交互层。
5. **焦点和输入模式是否正确？** 新弹窗用了 UI Only，却把关闭动作留在 Player Controller 里，可能打不开关闭键；给菜单自身接关闭处理或交由统一 HUD 管理。

Visibility 的 **Hidden** 隐藏但通常保留布局空间，**Collapsed** 不占布局空间；**Visible** 可显示且参与命中。这些与“移出屏幕”不是同一种状态。

默认 Esc 可能被 UE 编辑器用来停止 PIE。排查游戏暂停键时可用独立游戏窗口，或调整编辑器 Stop Play 快捷键；不要因此误判游戏没有绑定暂停。

### 10. 血条、列表与数据：不要只改预览数字

#### 10.1 进度条

Progress Bar 的 Percent 使用 0～1。要显示一个属性，通常计算“当前值 / 最大值”，最大值必须大于 0，再限制在 0～1；通过 `Set Percent` 更新。

现有 `HealthBar`、`TransformationBar` 已由程序持续刷新。你可以改样式、方向、尺寸和颜色，但不能在 Designer 把 Percent 改成 1 就让玩家加满血。右上 `SprintRing` 是项目自己的 `DWProgressRing`，不要把它删除后换成同名普通 Image，类型不符会断开原绑定。

#### 10.2 一份格子模板，不等于一份背包数据

`WBP_DWInventorySlot` 描述“一个格子长什么样”；背包里有多少格、每格什么物品，是数据与逻辑决定的。改这一份模板会影响使用它的所有格子。

1. 先只改原模板的外观，保留 `SlotButton`、`ItemIcon`、`ItemNameText`、`QuantityText` 的名字和类型。
2. 想换另一套模板时，在主 WBP 类默认值的 `InventorySlotClass` 指定兼容副本。
3. 主 WBP 的两个容器 `InventoryGrid` 与 `CraftingInventoryGrid` 保留，清掉它们下面旧的预览格子，让运行时按新模板补建。
4. 列数改 `InventoryColumns`，总槽数改玩法配置的 `MaxInventorySlots`，二者不是一回事。
5. 在背包页和制作页分别查看；只检查一页可能漏掉另一份旧预览条目。

配方和存档条目同理。只在 Designer 加一行文字不会增加真实合成配方或存档槽。新增配方要写真实数据表；完整配表步骤见数据章节。

### 11. 加一个弹性入场动画

先在副本或自己新增的装饰容器上练习，避免与原生动画同时控制同一属性。

1. 在 Designer 底部打开 **Animations / 动画**抽屉，点击 `+ Animation` 添加一个动画并选中它。
2. 选中要运动的外层容器，把时间线停在开头，对 Render Transform 的 Scale、Translation 或 Render Opacity 添加关键帧。
3. 将播放头移动到后面的时刻，设置结束值并再次加关键帧。例如从略小、稍偏下到正常大小和位置；想有弹性，可在结束前安排一个略微放大的过冲帧，再回到 1。
4. 拖动时间线预览。确认两个时间点确实都有关键帧；只是移动控件而没加键，可能只改了静态布局。
5. 回 Graph，把动画引用连到 **Play Animation**。现有页面入场可从 `OnMenuPageChanged` 判断相应页面后触发。
6. 连续打开、关闭几次，确认从期望初始状态开始，不会停在上次中途的位置。

UMG 动画由动画轨道和属性关键帧组成，图表负责何时播放。[Epic：Animating UMG Widgets](https://dev.epicgames.com/documentation/unreal-engine/animating-umg-widgets-in-unreal-engine?lang=en-US)

动画不会自动等待页面逻辑：当前旧页面切换会立即隐藏旧页，想“先播完退出动画再关闭”，需要等待动画完成后再执行关闭。不要把 Play Animation 与 Remove from Parent 串在一起就期待看见退出过程。

物体上方的采集提示已经有原生弹性运动。先改 `DA_DWInteractionPromptStyle` 的 `AppearDuration`、`InitialScale`、`OvershootAmount`、`FloatDistance` 等参数；不要再给同一个根容器叠第二套缩放，让两边争抢数值。提示组件的 WorldOffset 是场景高度，FloatDistance 是界面动画位移，单位与用途不同。

### 12. 字体、中英文和文字资源

#### 12.1 全部 UI 换字体，应从入口改

1. 在内容浏览器打开 `Blueprints/BP_DWGameplayHUD`，点 Class Defaults，找到 **GameUIFont**。
2. 当前组合字体是 `UI/Interaction/Fonts/F_DWHandDrawn`，英文/数字使用 Lilita One，中文使用站酷快乐体。换字体时必须确认中文、数字和标点都有字形。
3. 将你准备好的 Font/Composite Font 资产赋给 GameUIFont，编译保存；单个 TextBlock 的字号仍在 Designer 改。
4. 物体上方提示另打开 `DA_DWInteractionPromptStyle`，同时设置 **TitleFont 和 KeyFont**。按键可能显示“空格”“左键”，不能让按键字体仍只支持英文。
5. 重新运行，查看标题、设置下拉、背包物品名、数量和提示。只在 Designer 改一个 TextBlock 的字体资产，可能被运行时的全局入口覆盖。

导入 TTF/OTF 后，需要可用的 FontFace/Font 资产；只把字体文件放在资源管理器目录里不会让 UMG 自动使用它。混合字体可在 Composite Font 中设置默认英文字体、中文范围和 fallback。出现方框先查字形覆盖与实际引用，不要只放大字号。新增 Rich Text Block 还需要自己的样式表，不会自动获得现有普通 TextBlock 的所有处理。

#### 12.2 新文字如何支持中文和英文

物品与配方：分别在 `DT_DWItems`、`DT_DWRecipes` 填 `DisplayName` 和 `EnglishDisplayName`。ID 是稳定逻辑标识，不翻译。

固定按钮文字：本项目使用自己的双语对照库。改 Designer 中文后，需要同步 `DWLocalizationCatalog.inl` 的中文匹配键与英文，由程序编译；不是写完中文就会自动翻译。

自建的小界面可以在蓝图里用 `DWLocalizationLibrary.GetLanguage` 选择中文或英文文本，在创建时刷新，并在语言变化时再刷新。现有主 WBP 有 `OnLanguageChanged` 事件；独立 WBP 可绑定 `DWLocalizationSubsystem.OnLanguageChanged`，关闭/销毁时解除自己新增的绑定。

游戏设置里切换语言会立即保存，不需要点应用；这不改变 UE 编辑器语言，也不改变 ItemId。英文往往比中文长，切语言后重新检查按钮宽度、自动换行和详情面板。

新按键文字用玩家控制器的 **GetActionKeyLabel** 取得当前已生效的按键，不把“F/B/Tab”写死在自己做的贴图里。已有 `ControlsHint` 等文本会动态更新，改预览文字不会替换运行时按键。

### 13. 点击、合成、开背包和 BGM 的声音怎么接

#### 13.1 先用现成槽位

1. 将 WAV 导入玩法 Audio 目录下你建立的子目录，得到 Sound Wave；需要随机音高或多个样本时，可准备 Sound Cue。
2. 打开 `Data/DA_DoughWorldGameplay`，搜索对应字段。
3. 给 `UIClickSound` 填点击声，给 `InventoryOpenSound` / `InventoryCloseSound` 填背包开关声，给 `CraftSuccessSound` / `CraftFailedSound` 填制作结果声。
4. BGM 填 `MenuBGM` 与 `GameplayBGM`。提示出现声则在 `DA_DWInteractionPromptStyle.AppearSound`，不是主表的点击槽。
5. 保存并重新运行一次对应操作，听响度、是否重复播放、是否存在循环尾音。全部 28 个事件槽和音量分组在第 3 章列出。

主界面现有按钮和页面已经有声音逻辑，不必在所有 OnClicked 再加一次播放。新加的普通按钮不会自动获得悬停或点击音，按需要调用已有 `PlayClick` / `DWAudioLibrary.PlayEvent`；声音节点只播放声音，不会代替制作或存档操作。

不要在 Tick、`OnHUDValuesChanged` 或每次列表刷新时无条件播放点击声。它们可能频繁执行，造成一秒响很多次。一次性按钮反馈也不要填无限循环音频。

#### 13.2 长按采集和音乐不要做出第二份播放器

现有采集已负责开始、循环、成功、停止与失败音。自己再在蓝图创建一个循环播放器，容易出现松开后还在响。主 HUD 同样管理 BGM 的启动、切换和释放；本阶段通过现成音频槽换曲即可。

当前设置只有主音量，不是独立音乐与音效两个滑条。新增两组音量需要保存与播放控制一起接入，仅在 Designer 拖两个 Slider 不能完成。

### 14. 最后用这份清单确认自己真的改成功了

1. 编译保存所有改过的 WBP/蓝图、数据表、字体与贴图；退出并重新开始 Play。
2. 标题到存档，再进入游戏；背包、制作、暂停和设置都能打开、关闭。
3. 按钮 Normal/Hovered/Pressed/Disabled 都显示正确，一次点击只执行一次。
4. 采集的物品在背包和制作页都显示新图标；数量仍来自真实数据。
5. 中英文切换后无方框、无重要文字被截断；按键提示与实际设置一致。
6. 两种分辨率和较窄窗口下主要按钮没有移出屏幕，装饰图片不挡鼠标。
7. 点击、合成、面板开关声音只在对应时机播放，退出菜单或松开采集后没有意外残留。
8. 检查你的改动是否真的被正式引用。副本预览很好看，但游戏仍用原 WBP 时，属于引用还没接上。

本章的图形界面教学与项目接口说明不等于已经替所有新 UI 完成游戏逻辑或打包验证。先做一个小改动并确认生效，再继续下一项，比同时改布局、名称、父类和输入更容易定位问题。


## 第二部分 · 开场与主菜单示例（完整手册第 7 章）

本章对应独立的主菜单示例。它把「黑屏 → Logo 弹性出现 → Logo 停留 → Logo 淡出 → 显露 3D 背景，同时菜单弹性进入」连成一个可修改的流程。外观和关键帧保存在真正的 UMG 控件蓝图里；阶段推进、跳过、暂停兼容和原有存档菜单衔接由 C++ 父类处理。原来的游玩界面 `WBP_DWGameplay` 继续用于玩法地图。

先按第 1–9 节修改本例。如果你想学习完全不用本例 C++ 的实现，第 10 节给出了从空白蓝图搭建同类开场的独立教学。那一节不是要求你再给现有示例重复连接一次。

### 1. 打开哪些资产

本章所有新增示例资产统一位于 `Content/DoughWorld/Maps/Gameplay/Frontend`，也就是 `/Game/DoughWorld/Maps/Gameplay/Frontend`。

| 资产 | 用途 | 从哪里改 |
|---|---|---|
| `Levels/L_DWMainMenu_Showcase` | 主菜单后面的真实 3D 展示关卡 | 关卡编辑器：场景物体、灯光、相机 |
| `UI/WBP_DWFrontend` | 新开场及菜单的外观、动画、时长和 Logo 槽位 | Designer、Animations、Class Defaults |
| `Blueprints/BP_DWFrontendHUD` | 创建新界面，选择展示相机，沿用原有标题、设置和存档操作 | Class Defaults |
| `Blueprints/BP_DWFrontendGameMode` | 本关卡使用的 HUD、Controller、Pawn 组合 | Class Defaults；关卡 World Settings |
| `Materials/M_Menu_*` | 示例场景的简单地面、路径等材质 | 材质编辑器 |

原有全局配置仍是 `/Game/DoughWorld/Maps/Gameplay/Data/DA_DoughWorldGameplay`。`MainMenuMap` 指向新标题关卡；`GameplayMap` 仍指向实际游玩地图。这是两个不同的入口。

最直接的查看方式：停止运行 → 在内容浏览器打开 `L_DWMainMenu_Showcase` → Play。不要把 Designer 内播放某一个动画，等同于整个开场已经运行；Designer 用来检查关键帧，Play 用来检查完整阶段和按钮操作。

### 2. 现有开场怎样运行

默认阶段如下，单位都是秒：

| 阶段 | 默认时长 | 实际表现 |
|---|---:|---|
| BlackHold | 0.45 | 黑屏，Logo 还不可见 |
| LogoIn | 0.55 | Logo 透明度从 0 到 1，缩放从 0.85 弹到 1.04，再回到 1 |
| LogoHold | 1.25 | Logo 保持完整可见 |
| LogoOut | 0.40 | Logo 淡出 |
| Reveal | 0.65 | 黑幕淡出，显露后面的 3D 场景 |
| MenuEnter | 0.65 | 与 Reveal 同时开始；标题与三个按钮依次弹性进入 |
| Ready | 持续 | 开始、设置、退出按钮可以操作 |

默认总时长约 `0.45 + 0.55 + 1.25 + 0.40 + max(0.65, 0.65) = 3.30 秒`。最后两个阶段同时播放；输入要等它们都结束才完全开放。

播放期间，右下角的「跳过 / Skip」按钮、空格和 Esc 可以跳过。`Allow Skip` 关闭后，玩家的这些跳过输入被关闭；蓝图主动调用 `Skip Intro` 仍然有效。跳过会收好黑幕、Logo 和隐藏按钮，直接落到完整菜单，不会顺便点击“开始”。

从设置或存档页退回标题页，会重播菜单进入动画，不会重新播放整段黑屏和 Logo。从游玩关卡重新打开主菜单，则创建新的界面，由下面的重播选项决定是否再次播放完整 Logo。

### 3. 最常改的参数都在 Class Defaults

打开 `WBP_DWFrontend` → 切到 Graph → 点击顶部 **Class Defaults / 类默认值**。右侧搜索 `Frontend`，或直接搜索下表属性的英文片段。改完后 Compile → Save → 停止并重新 Play。

| 属性名 | 默认值 | 用途 |
|---|---:|---|
| `Play Intro On Construct` | true | 创建此界面时是否自动播放开场 |
| `Replay Intro When Returning To Menu` | true | 新建此界面时是否每次重播完整开场；关闭后每个 GameInstance 会话只自动播放一次 |
| `Black Hold Seconds` | 0.45 | 开头纯黑停顿 |
| `Logo In Seconds` | 0.55 | Logo 出现耗时 |
| `Logo Hold Seconds` | 1.25 | Logo 完整显示的停留时间 |
| `Logo Out Seconds` | 0.40 | Logo 消失耗时 |
| `Reveal Seconds` | 0.65 | 黑幕淡出耗时 |
| `Menu Enter Seconds` | 0.65 | 标题和按钮进入耗时 |
| `Allow Skip` | true | 玩家是否能通过按钮、空格、Esc 跳过 |
| `Logo Texture` | 空 | 新 Logo 图片接口；空时使用文字占位 |
| `Show Logo Text With Texture` | false | 图片存在时，是否同时显示 `LogoText` |
| `Menu Blur Strength` | 5 | 场景后景模糊强度，0 为不模糊 |
| `Logo Sound` | 空 | Logo 开始出现时的声音 |
| `Menu Reveal Sound` | 空 | 菜单开始进入时的声音；提前跳过时也会补播一次 |
| `Sound Volume` | 1 | 上面两个声音的音量乘数，编辑范围 0–2 |

`Replay Intro When Returning To Menu=false` 的“一次”是本次游戏进程内当前 GameInstance 的一次，未写入三个游戏存档。结束 PIE 再运行会建立新会话。它也不会阻止你主动调用 `Replay Intro`。

#### 3.1 改播放速度，还是改动画形状

**改变整个动作快慢，优先改 `... Seconds`。改变弹跳幅度、按钮先后顺序，改动画时间轴。** 两者分工如下：

- 时间轴保存动作的相对节奏、透明度、位移和缩放曲线。
- 运行时读取动画长度，以 `动画原长度 ÷ 对应 Seconds` 为播放倍速。
- 例如 `Anim_MenuEnter` 原始时间轴长约 0.90 秒，但默认 `Menu Enter Seconds=0.65`，运行时会在约 0.65 秒内播完。改为 1.20 后，会按约 1.20 秒播放。
- 只把动画时间轴整体拉长，同时保持 `Menu Enter Seconds` 不变，运行总耗时仍由这个参数决定。要让玩家真正多等一秒，应改时长参数。
- 设为 0 代表跳过该段耗时，父类会设定最终可见状态。不是要删除动画或给它设一个零长度轨道。

本例按真实经过时间推进，即使原有菜单让游戏世界暂停，开场仍可以完成。不要另加一个重复的 `Event Tick` 来推进同一批透明度，否则会与动画争夺数值。

### 4. 更换 Logo、标题、字体和按钮图片

#### 4.1 推荐的 Logo 图片替换步骤

1. 把透明背景 PNG 导入 `Frontend/UI/Textures` 或你在该目录下新建的 Logo 文件夹。
2. 打开贴图，检查透明通道和边缘；UI 贴图使用适合 UI 的 Texture Group。UE 5.8 里某些旧教程中的 `UserInterface2D` 压缩选项显示为 `Uncompressed (RGBA8)`，以当前编辑器实际名称为准。
3. 打开 `WBP_DWFrontend` 的 Class Defaults，把图片拖入 **Logo Texture**。
4. 切回 Designer，在层级选择 `LogoImage`，修改 Canvas Slot 的尺寸和位置。默认图片区域约 `500 × 152`；请按自己 Logo 的宽高比调整，避免把方形 Logo 拉成长条。
5. 默认 `Show Logo Text With Texture=false`，图片出现时会隐藏 `LogoText`。如果需要“图形标志＋工作室文字”，打开此选项，再分别摆好图片和文字的位置。
6. 编译、保存，重新 Play，观察黑底上的透明边缘、位置和进出动画。

**Logo 图片的优先级要记住：** 运行时 `Logo Texture` 非空，会通过 `Set Brush From Texture` 覆盖 `LogoImage` 的 Brush；它为空时，运行时会隐藏 `LogoImage`、显示文字。因此只在 Designer 中给 `LogoImage → Brush → Image` 换图，运行时不一定看得到。默认工作流是用 `Logo Texture` 管图片、Designer 管摆放。

如果要完全改成 Designer 自己管理图片，需明确调整父类的 `ApplyLogoArtwork` 行为，或在独立纯蓝图版中自行管理。不要靠反复设置 Visibility 抵消现成规则。

#### 4.2 修改占位文字

- Logo 文字：Designer 选中 `LogoText`，把 `YOUR TEAM` 改成你的团队名。
- Logo 下方小字：选中 `FrontendPresentsText`，修改 `P R E S E N T S`。
- 游戏名称、副标题：打开 `BP_DWFrontendHUD → Class Defaults`，改 **Game Title / Game Subtitle**。运行时它们会写入 `TitleText / SubtitleText`，因此只改 Designer 里的这两个文本会被 HUD 覆盖。
- 其他按钮、Tagline 等文本沿用父类已有的本地化规则；需要新文字的中英对照时，按第 3 章的语言接口处理。

#### 4.3 字体和按钮皮肤

所有文字继续使用已有统一字体规则。改整套字形，进入 `BP_DWFrontendHUD → Class Defaults → Game UI Font`；只改字号、描边、阴影、间距，在 Designer 选中相应 TextBlock。HUD 会统一替换字体对象，但保留各个控件的字号等样式。

三个主按钮仍叫 `StartButton`、`TitleSettingsButton`、`QuitButton`。选中按钮，在 **Style** 下分别替换 Normal、Hovered、Pressed 状态的 Brush；需要九宫格伸缩的框，设置合适的 Draw As / Margin。文字是按钮里的子 TextBlock，需展开按钮选中后单独改字号和颜色。

本例初始样式是金色主按钮、奶油色次按钮、透明标题容器。为保留 3D 场景，`Title Background Texture` 通常保持为空，`TitleBackgroundImage` 保持 Collapsed。给 HUD 的 `Title Background Texture` 填入不透明整屏图，会把后面的 3D 场景挡住。

### 5. 编辑真正的 UMG 动画时间轴

#### 5.1 打开动画面板

1. 双击 `WBP_DWFrontend`，切到 Designer。
2. 打开底部 **Animations / 动画** 面板；布局不同可在 Window 菜单中查找 Animations。面板收起时先展开，不要误以为这个 WBP 没有动画。
3. 在动画列表选中 `Anim_MenuEnter`。时间轴会显示它绑定的 `TitlePage`、`StartButton`、`TitleSettingsButton`、`QuitButton`。
4. 拖动时间指针预览，展开某个控件的 **Render Opacity** 或 **Render Transform** 子轨道。
5. 选中关键帧修改数值；想在新时刻添加关键帧，把播放头移过去，再通过属性旁的关键帧按钮或轨道操作添加。仅修改详情中的静态值，不一定会写进当前动画。
6. Compile、Save，再在真实关卡 Play 中检查。Designer 只播放选中的一条动画，不会自动运行另外三条或等待停留阶段。

Logo 与黑幕的静态 Visibility 默认是 Collapsed，方便平时编辑菜单。如果在 Designer 预览 Logo 动画看不到东西，可临时把 `LogoLayer` 改为 Visible，并把其 Render Opacity 临时设为 1。检查后恢复静态布局；运行时父类会按阶段控制 Visibility。`BlackLayer` 临时设为 Visible 后会盖住整个画布，选层级里的 `LogoLayer`、`LogoText` 来继续编辑，不要在画布上反复点黑幕。

#### 5.2 四条动画各自负责什么

| 动画 | 原始时长约 | 绑定对象及属性 | 修改建议 |
|---|---:|---|---|
| `Anim_LogoIn` | 0.55 秒 | `LogoLayer`：Opacity、Translation Y、Scale X/Y | 控制 Logo 出现、轻微上浮和回弹 |
| `Anim_LogoOut` | 0.40 秒 | `LogoLayer`：Opacity | 控制 Logo 消失；尽量不再改变整体位置 |
| `Anim_Reveal` | 0.65 秒 | `BlackLayer`：Opacity 1 → 0 | 控制黑幕揭开，露出后面的真实场景 |
| `Anim_MenuEnter` | 0.90 秒 | `TitlePage` 和三个按钮：Opacity、Translation Y、Scale X/Y | 控制整体入场和三个按钮错时出现 |

`Anim_Reveal` 与 `Anim_MenuEnter` 同时播放，分别控制黑幕和菜单。Logo 的出现和消失都控制 `LogoLayer`，它们按顺序播放。不要把 `TitlePage` 或 `LogoLayer` 移进 `BlackLayer` 下面，否则黑幕自身透明度会连带影响其子控件。

#### 5.3 跟着改：让菜单回弹更轻

1. 选择 `Anim_MenuEnter`，展开 `TitlePage → Render Transform → Translation → Y`。
2. 当前大致是：0 秒在 Y=48，0.40 秒到 Y=-8，0.64 秒回到 Y=3，0.90 秒归零。
3. 先把中间的 -8 改为 -3，把 3 改为 1，保留起点和终点。这样仍会弹，但不那么晃。
4. 展开 Scale X/Y。把中间的 1.018 和 0.996 都往 1 靠近，例如 1.008 和 0.999；两轴一起改，避免压扁。
5. 确认末帧 Translation=(0,0)、Scale=(1,1)、Opacity=1。运行时结束后也会恢复这个终态，所以若动画最后停在其他尺寸，结束时就可能出现一次跳变。
6. 若只是想整体慢一点，回 Class Defaults 改 `Menu Enter Seconds`，不要把每个关键帧重新挪一次。

#### 5.4 跟着改：三个按钮依次出现

初始错开值是 Start 约 0.08 秒、Settings 约 0.16 秒、Quit 约 0.24 秒开始。选中某个按钮的 **全部对应轨道关键帧** 一起移动，保持它的透明度、位移和缩放互相同步。

如果只延后透明度，按钮可能已经在不可见时完成位移，出现后看不到弹跳。如果只延后位移，按钮会先亮在旧位置再突然移动。修改完成后保证所有按钮末帧都是完全可见、缩放 1、位移 0，且都落在动画播放范围内。

#### 5.5 编辑时不要改掉这些名称

`BlackLayer`、`LogoLayer`、`LogoImage`、`LogoText`、`MenuBlur`、`TitlePage`、`SkipIntroButton` 以及四条 `Anim_...` 名称用于父类绑定。可以改外观和容器内子内容；随意重命名这些绑定对象会让该部分效果失效。删掉动画时父类有简单回退表现，但那不代表你的自定义关键帧还在工作。

原有 `MenuRoot`、`PageSwitcher`、三枚主按钮也继续用于已有功能。PageSwitcher 的索引仍是：0 标题、1 存档、2 背包、3 制作、4 暂停、5 设置、6 死亡。不要因为菜单例子暂时不显示背包，就删除或重排中间页面。

### 6. 改 3D 背景与相机

#### 6.1 画面后面是真实关卡

打开 `L_DWMainMenu_Showcase` 后，可以直接移动建筑、树、路径和灯光。它们是关卡 Actor，不是 UI 图片。想改变构图，移动场景里的相机；想改变按钮在屏幕上的位置，则去 WBP 改 `TitlePage` 的布局，两者不要混淆。

菜单主体初始偏左，所以更适合把建筑或视觉焦点安排在画面的中右侧。`TitlePage` 是 Overlay 下的元素：改它的 Overlay Slot 对齐和 Padding 可以调整左右位置；改 `TitlePage_Size` 的宽度可以控制整组菜单宽度。按钮处于 Vertical Box 中，间距用 Vertical Box Slot Padding。

#### 6.2 为什么一定要有 DWMenuCamera 标签

示例 HUD 默认按 `Menu Camera Tag=DWMenuCamera` 查找 **CameraActor**。选择你要作为标题背景的相机，在 Details 搜索 **Tags**，展开 **Actor → Tags**，加一个值 `DWMenuCamera`。这是 Actor 标签，不是 Component Tags。

HUD 的选择顺序是：有效的 `MenuCameraOverride` → 匹配 Actor Tag 的相机 → 同名对象；编辑器中也允许同名 Actor Label 作为回退。正式使用应保留 Actor Tag，因为编辑器 Label 不是打包后可靠的查找依据。同一关卡只保留一个相机使用这个标签，避免多个都匹配时选到不期望的那个。

`BP_DWFrontendHUD → Class Defaults` 可以改 `Menu Camera Tag`。若改成 `MyMenuCamera`，关卡相机也要同步添加这个 Actor Tag。`Menu Camera Override` 是实例级直接指定的接口，HUD 通常由 GameMode 自动生成，因此日常编辑更方便使用 Tag。

`Keep Menu Camera Active=true` 会维持该相机作为视角。如果你以后要用 Level Sequence 切到其他相机，应先关闭它，或在切换流程中显式交接相机控制权，否则 HUD 可能把画面重新切回来。

#### 6.3 复制成自己的标题关卡

1. 在内容浏览器复制 `L_DWMainMenu_Showcase`，命名如 `L_DWMainMenu_MyVersion`，仍放在 `Frontend/Levels`。
2. 打开副本，改场景和构图；保留有效的 `DWMenuCamera` 标签。
3. 打开 World Settings，确认 **GameMode Override = BP_DWFrontendGameMode**。
4. 保存关卡，把全局配置 `DA_DoughWorldGameplay → Main Menu Map` 改为副本完整包路径，例如 `/Game/DoughWorld/Maps/Gameplay/Frontend/Levels/L_DWMainMenu_MyVersion`。这里不要写磁盘的 `.umap` 路径，也不写 `_C`。
5. 如果希望启动程序就进这个标题图，再去 Project Settings → Maps & Modes，把 **Game Default Map** 改为该图；**Editor Startup Map** 只影响打开编辑器时先显示哪张图，作用不同。
6. 打包时确认标题图和 GameplayMap 都会被 Cook/打包，包括新复制的关卡。已有字符串路径不等同于自动建立硬引用；在打包地图列表中明确包含最终入口。
7. Play 验证新建/加载进入 GameplayMap，然后“返回标题”真的回到你的副本。

原存档验证仍要求保存地图等于 `GameplayMap`。只替换 `MainMenuMap` 不需要迁移游玩存档；不要为了修改菜单背景，把 `GameplayMap` 也顺手换成展示图。

### 7. GameMode、HUD、Widget 和配置怎样接起来

以下是示例的接线关系。某一层选错，通常会表现为旧 UI、没有界面、看见默认相机或生成多余角色。

| 位置 | 字段 | 示例值 |
|---|---|---|
| 展示关卡 World Settings | GameMode Override | `BP_DWFrontendGameMode` |
| `BP_DWFrontendGameMode` | 父类 | `GameModeBase` |
| 同上 | HUD Class | `BP_DWFrontendHUD` |
| 同上 | Player Controller Class | `DWPlayerController` |
| 同上 | Default Pawn Class | None；展示关卡不生成游玩角色 |
| `BP_DWFrontendHUD` | 父类 | `DWFrontendHUD` |
| 同上 | Widget Class | `WBP_DWFrontend` |
| 同上 | Show Title On Start | true |
| 同上 | Menu Camera Tag | `DWMenuCamera` |
| `WBP_DWFrontend` | Parent Class | `DWFrontendWidget` |
| 项目 Maps & Modes | Game Instance Class | 保留 `DWGameInstance` |
| `DA_DoughWorldGameplay` | Main Menu Map | `/Game/DoughWorld/Maps/Gameplay/Frontend/Levels/L_DWMainMenu_Showcase` |
| 同上 | Gameplay Map | 实际游玩关卡，例如原 `L_DoughWorld_GameplayPrototype` |

HUD 的 `Widget Class` 明确填写时优先；为空时，`Frontend Widget Class Path` 才作为加载回退。若复制了 WBP，推荐直接把 HUD 的 `Widget Class` 指向副本。软类路径回退字段需要完整对象类路径，例如 `/Game/.../WBP_MyMenu.WBP_MyMenu_C`，与地图字符串的写法不同。

`BP_DWFrontendHUD` 沿用原 HUD 的界面管理和三槽存档流程，`WBP_DWFrontend` 则保留原 WBP 的完整控件树和页面顺序。StartButton 仍打开现有存档页，再由现有的新建/加载操作进入游玩地图。它不会仅因为有新开场，就变成“直接 Open Level 跳过存档”。

只在标题图使用这个新 GameMode。游玩图仍应保留原玩家、原 Gameplay HUD 与输入链，不要把所有地图的默认 Pawn 都清空。项目 GameInstance 也不要换成空 GameInstance，否则配置、存档和语言流程会断开。

### 8. 音效、BGM 与蓝图扩展事件

Logo 和揭幕的专用声音在 `WBP_DWFrontend → Class Defaults → Frontend | Audio`。按钮点击、打开设置、存档页面、主菜单 BGM 等继续使用 `DA_DoughWorldGameplay` 的原有音频槽，包括 `MenuBGM` 与 UI 事件声音。改变 LogoSound 不会自动替换所有按钮点击声。

下面这些接口可以从 `DWFrontendWidget` 子蓝图使用：

| 接口 | 含义 | 合适用途 |
|---|---|---|
| `Replay Intro` | 重新开始完整开场 | 开发时添加一个测试按钮 |
| `Skip Intro` | 立即结束当前开场 | 自定义跳过交互 |
| `Play Menu Entrance` | 只播标题和按钮进入动画 | 自定义返回标题表现；已有返回流程会自动调用 |
| `Is Intro Playing` | 读取是否仍在开场 | 决定其他 UI 是否暂时响应 |
| `Get Intro Phase` | 读取当前阶段 | 调试阶段或显示测试文本 |
| `On Intro Phase Changed` | 阶段变化时触发的蓝图事件 | 某阶段开始时播放补充装饰效果 |
| `On Intro Finished(bSkipped)` | 开场完成或被跳过时触发 | 根据是否跳过安排后续表现 |

本例在创建时已自动播放、绑定 Skip 按钮、控制声音和按键。**不要再在 Event Construct 连接一套 Play Animation + Delay 链，也不要再给同一个 Skip 或 Start 按钮重复绑定原操作。** 需要增加粒子、额外文字或相机细节时，从上述扩展事件接附加表现，并保持一个明确的流程控制源。

### 9. 修改后按这份清单验证

1. 打开展示关卡 Play：黑屏、Logo、淡出、场景、菜单顺序正确；总时长与参数一致。
2. 分别在黑屏、Logo 出现、Logo 停留、菜单进入时跳过：没有残留黑幕、半透明按钮或持续缩放；一次跳过不会直接开始游戏。
3. 点设置，再返回：只播菜单进入，不重复 Logo；语言和分辨率设置仍可用。
4. 点开始：原有 3 个存档槽仍可浏览，新建/加载仍进入 GameplayMap；从游戏返回标题指向 MainMenuMap。
5. 换 Logo 后确认运行时图片存在；检查宽高比、透明边缘与 `Show Logo Text With Texture`。
6. 改 `Menu Enter Seconds` 为明显不同的值，例如 1.5 秒：确认实际变慢。还原到你满意的参数。
7. 调整窗口到 1280×720、1920×1080 和较窄比例：菜单可见，按钮不被黑幕或其他透明层挡住。
8. 如果要交付可执行程序，再在打包版本检查地图收录、字体、声音、相机 Tag 和资源加载；编辑器 Play 通过不能代替打包检查。

| 现象 | 优先检查 |
|---|---|
| 开场还是旧菜单 | 当前关卡 GameMode Override；HUD Class；HUD Widget Class |
| Logo 图只在 Designer 看得到 | `Logo Texture` 是否赋值；运行时该接口会覆盖 Brush 并控制 Visibility |
| 标题文字修改后又变回去 | `BP_DWFrontendHUD.GameTitle / GameSubtitle` 覆盖 Designer 文本 |
| 改了动画长度，实际播放没有变慢 | 同步修改对应的 `... Seconds` 参数 |
| 动画结束时突然缩放或跳位置 | 末帧需 Scale=1、Translation=0、Opacity=1；父类会恢复这些终态 |
| 菜单后面一片黑或默认空视角 | CameraActor 是否有 Actor Tag；HUD 是否匹配；光照和曝光；BlackLayer 是否按结束状态隐藏 |
| 场景被静态图片挡住 | HUD 的 Title Background Texture 和 TitleBackgroundImage 可见性 |
| 换到另一台相机后又跳回来 | `Keep Menu Camera Active` 与自定义相机切换同时控制视角 |
| 字体换了又恢复 | HUD Game UI Font 的统一字体规则 |
| 出现两遍 Logo 或两次音效 | 是否额外在 Construct、按钮 OnClicked 或阶段事件重复启动流程 |

### 10. 从空白搭一个纯蓝图版：独立教学

这一节教你掌握同类效果的原理。新建一套练习资产，父类使用 UE 自带 `UserWidget / PlayerController / GameModeBase`，不继承 `DWFrontendWidget`，也不调用本例的 C++ 接口。它实现开场、跳过、3D 背景和菜单导航；正式项目的三槽存档、背包、语言等仍由前面已有系统负责，不能把这个教学菜单直接替换进去后就当成完整游戏接线。

#### 10.1 建立练习资产

在例如 `Frontend/Learning` 中创建：

- `WBP_LearnFrontend`：父类 UserWidget。
- `BP_LearnMenuController`：父类 PlayerController。
- `BP_LearnMenuGameMode`：父类 GameModeBase；Default Pawn Class=None，Player Controller Class=BP_LearnMenuController，HUD Class 使用普通 HUD 即可。
- `L_LearnMenu`：空关卡，放场景、灯光和一台 CameraActor，Actor Tag=`LearnMenuCamera`；World Settings 指向 BP_LearnMenuGameMode。
- 蓝图枚举 `E_LearnIntroPhase`：BlackHold、LogoIn、LogoHold、LogoOut、Reveal、Ready。

本教学采用**不暂停世界的独立标题关卡**，因此普通 Timer 和动画完成事件能正常推进。这里没有游玩 Pawn，也没有需要冻结的战斗。若你以后把同一教学流程移进暂停的游戏关卡，应重新处理暂停时的计时和 Tick；不能直接认为普通 Delay/Timer 仍照常流逝。

#### 10.2 在 Designer 搭层级

根 Canvas 下建这些兄弟层，按 Z Order 从下到上：

| 控件 | 类型与主要设置 | Z Order |
|---|---|---:|
| BackgroundBlur | Background Blur；全屏锚点、Offsets=0 | 0 |
| MenuRoot | Border 内放 WidgetSwitcher；全屏 | 10 |
| BlackLayer | Border；黑色 Brush；全屏 | 100 |
| LogoLayer | Border，里面放 Image/文字；居中 | 110 |
| SkipButton | Button；右下角，文字“跳过” | 120 |

MenuRoot 的 WidgetSwitcher 先做两页：TitlePage（标题＋开始/设置/退出）和 SettingsPage（设置占位＋返回）。LogoLayer 与 BlackLayer 同级。LogoLayer 的 Visibility 用 Not Hit-Testable，不挡 Skip；黑幕可 Visible，在开场期间挡住下面的菜单。

所有准备从 Graph 引用的控件勾选 **Is Variable**。WBP 的 Class Defaults 打开 **Is Focusable**，供键盘跳过使用。

创建四条动画，名字可与本例相同，轨道与第 5 节一致。为了先学清楚，把动画原始长度直接设为所需时间，Play Animation 的 Playback Speed 使用 1；等基本流程通了，再使用“原长度 ÷ 目标时长”接可调倍速。

#### 10.3 在 Controller 创建界面并选相机

`BP_LearnMenuController → Event BeginPlay` 按以下顺序连接：

```text
Event BeginPlay
  → Get All Actors Of Class With Tag（CameraActor，LearnMenuCamera）
  → 数组 Length > 0 的 Branch
      true：Get[0] → Set View Target With Blend（Target=Self，NewViewTarget=相机，BlendTime=0）
  → Create Widget（Class=WBP_LearnFrontend，Owning Player=Self）
  → Promote to Variable：MenuWidget
  → Add To Viewport
  → Set Show Mouse Cursor=true
  → Set Input Mode UI Only（PlayerController=Self，WidgetToFocus=MenuWidget）
  → MenuWidget.Set Keyboard Focus
```

把相机分支和 CreateWidget 的后续执行线正确汇合，别让“没有相机”分支连 UI 都不创建。只让 Controller 创建一次 WBP；不要又在 Level Blueprint 重复 CreateWidget。

#### 10.4 WBP 变量和一次性动画完成绑定

在 WBP 建立变量：

- `Phase`：E_LearnIntroPhase。
- `bIntroPlaying`：Boolean。
- `bRevealDone`、`bMenuDone`：Boolean。
- `BlackHoldSeconds`=0.45，`LogoHoldSeconds`=1.25。
- `BlackTimerHandle`、`LogoTimerHandle`：Timer Handle。
- `LogoSound`、`MenuRevealSound`：SoundBase Object Reference，可留空。

在 **Event On Initialized** 中，为四条动画各执行一次 `Bind to Animation Finished`。Animation 分别接对应动画，Delegate 分别接自定义事件 `LogoInFinished / LogoOutFinished / RevealFinished / MenuFinished`。这些绑定只做一次；不要每次返回菜单都重新 Bind。

再从 **Event Construct** 调用自定义事件 `BeginIntro`。Construct 可能在重新加入界面时再次执行，所以它可以启动演出，但绑定应留在一次性的 OnInitialized。

#### 10.5 把阶段顺序连起来

`BeginIntro`：

```text
Clear and Invalidate Timer by Handle（两枚 Timer Handle）
→ Stop All Animations
→ Set bIntroPlaying=true
→ Set Phase=BlackHold
→ MenuRoot.SetIsEnabled(false)
→ MenuRoot.SetRenderOpacity(0)
→ BlackLayer.SetVisibility(Visible)，SetRenderOpacity(1)
→ LogoLayer.SetVisibility(Not Hit-Testable)，SetRenderOpacity(0)
→ SkipButton.SetVisibility(Visible)
→ Set Timer by Event（Time=BlackHoldSeconds，Looping=false）
   回调 BeginLogoIn；返回的 Handle 存到 BlackTimerHandle
```

`BeginLogoIn`：先 Branch 检查 `bIntroPlaying && Phase==BlackHold`，成立才 `Phase=LogoIn → Play Sound 2D(LogoSound，若有效) → Play Animation(Anim_LogoIn)`。

`LogoInFinished`：检查 `bIntroPlaying && Phase==LogoIn`，成立才 `Phase=LogoHold → LogoLayer.Opacity=1 → Set Timer by Event(LogoHoldSeconds，false)`，回调 `BeginLogoOut`，保存 LogoTimerHandle。

`BeginLogoOut`：检查仍是 LogoHold 且仍播放中，成立才 `Phase=LogoOut → Play Animation(Anim_LogoOut)`。

`LogoOutFinished`：检查仍是 LogoOut，成立才调用 `BeginReveal`：

```text
Phase=Reveal
→ LogoLayer.Visibility=Collapsed
→ MenuRoot.RenderOpacity=1
→ bRevealDone=false；bMenuDone=false
→ Play Sound 2D（MenuRevealSound，若有效）
→ Sequence
    Then 0：Play Animation(Anim_Reveal)
    Then 1：Play Animation(Anim_MenuEnter)
```

`RevealFinished`：只有 `bIntroPlaying && Phase==Reveal` 才设 `bRevealDone=true`，然后调用 `TryFinishIntro`。`MenuFinished` 同理设 `bMenuDone=true`。`TryFinishIntro` 检查两者都 true 才调用 `FinishIntro`。这样黑幕淡出和菜单进入长度不同时，也不会提前允许玩家点击。

如果你允许停留时间为 0，Set Timer 的 Time=0 不能当成立即回调使用。先 Branch：大于 0 才设 Timer；否则直接调用下一阶段。这个细节也适用于其他可调计时。

#### 10.6 跳过一定要清理计时和终态

做 `FinishIntro`，先把 `bIntroPlaying=false`、`Phase=Ready`，再清理动画/Timer，防止停止过程中触发的回调继续进入旧阶段：

```text
bIntroPlaying=false；Phase=Ready
→ Clear and Invalidate Timer by Handle（两枚）
→ Stop All Animations
→ BlackLayer：Opacity=0，Visibility=Collapsed
→ LogoLayer：Opacity=0，Visibility=Collapsed
→ SkipButton：Visibility=Collapsed
→ MenuRoot：Opacity=1，IsEnabled=true
→ TitlePage 和三个按钮：Opacity=1，RenderTransform 恢复 Identity
```

Identity 表示 Translation=(0,0)、Scale=(1,1)、Shear=(0,0)、Angle=0。只设置菜单父层的 Opacity=1 还不够：如果按钮自己的动画停在 Opacity=0，它仍然看不见。

`SkipButton.OnClicked → Branch(bIntroPlaying) → FinishIntro`。键盘版在 WBP 的 **On Preview Key Down** override 中，若播放中且 Key 为 Space Bar 或 Escape，调用 FinishIntro 并返回 Handled；播放中其他按键可返回 Handled，结束后返回 Unhandled，让正常按钮处理输入。

#### 10.7 接菜单、声音和场景切换

教学版的最小连接：

- SettingsButton.OnClicked：Play Sound 2D 点击声 → WidgetSwitcher.SetActiveWidgetIndex(1)。
- SettingsBackButton.OnClicked：点击声 → SetActiveWidgetIndex(0) → Play Animation(Anim_MenuEnter)。
- QuitButton.OnClicked：点击声 → Quit Game（Specific Player=Owning Player，Quit Preference=Quit）。
- StartButton.OnClicked：点击声 → Set Input Mode Game Only、Show Mouse Cursor=false → Open Level (by Object Reference)，Level 指向一张专门的练习游玩关卡。
- 菜单 BGM 可在 Controller BeginPlay 使用 Spawn Sound 2D 并保存返回的 AudioComponent；切图前 Stop。需要循环时使用循环音频资产或 Sound Cue。不要每帧 Spawn。

这里的 Start 只是教学用切图。正式 Dough World 的 Start 需要保留已有“存档页 → 新建/加载 → 数据恢复”的流程；直接 Open Level 不会替你创建活动槽位、背包或存档数据。这个区别是教学菜单和可直接接入现有游戏菜单的边界。

练习验收：完整播完一次、四个不同时刻跳过、设置来回切换、分辨率变更、退出与切图。掌握后，你可以把纯蓝图流程当成自己的实现；也可以继续使用本例的 C++ 父类，只在 Designer 和动画轨道上改视觉。不要让两套开场状态机同时控制同一个 WBP。

### 11. 实现与维护定位

本章依据 `Source/GDATtest/Gameplay/DWFrontendWidget.h/.cpp`、`DWFrontendHUD.h/.cpp`、`DWFrontendAuthoringLibrary.h/.cpp` 及现有 `DWGameplayWidget / DWGameplayHUD / DWGameInstance` 的实际字段编写。新增 Authoring 工具只负责第一次复制并创建示例资产；发现目的资产已存在时会保留它，避免覆盖 Designer 手工修改。以后日常调整请直接编辑 WBP，不要把“重新生成默认资源”当作保存操作。

`DWFrontendAuthoringLibrary` 创建的是可保存的 UMG 控件树与 MovieScene 关键帧，不是运行时用代码画出来的假按钮。`DWFrontendWidget` 仍负责流程、安全跳过和暂停兼容；如果你准备改变阶段数量、增加整段片头视频、接复杂加载流程或完全改变页面系统，应由程序调整该流程或明确切换到独立蓝图实现，并重新验证跳过和存档衔接。

本章中的验证清单是后续修改时的操作步骤。当前交付的编译、实际运行、截图和保存检查结果，应以本次示例的最终验证报告为准，不能仅凭手册步骤视为已通过全部测试。

### 12. 真实运行截图：从黑幕到可点击菜单

下面使用本工程实际运行截图。彩色编号框只是操作说明；没有替换截图里的 UI、文字或场景。顶部是预览窗口标题栏，不是游戏界面美术。需要放大时，可打开 `assets/FrontendExample/截图放大指引.html`。

#### 12.1 黑幕是流程的第一阶段

![图7-1：实际开场黑幕，右下角仍有“跳过 / Skip”](assets/FrontendExample/originals/01_Black.png)

这一阶段先覆盖游戏画面，再进入 Logo 淡入。右下角 **跳过 / Skip** 用于直接结束开场并显示菜单，不是退出游戏。修改黑幕持续时间应改前文的阶段时长，不必删除 BlackLayer。

#### 12.2 把 YOUR TEAM 换成你的团队 Logo

![图7-Logo：真实Logo可见帧，①是团队文字占位，②是跳过按钮](assets/FrontendExample/02_Logo_annotated.svg)

本例的 **YOUR TEAM / PRESENTS** 是文字占位。换成正式 Logo 时，按以下顺序操作：

1. 把带透明背景的 Logo PNG 导入 `/Game/DoughWorld/Maps/Gameplay/Frontend/UI/Textures`；没有此文件夹时可新建。
2. 打开 `/Game/DoughWorld/Maps/Gameplay/Frontend/UI/WBP_DWFrontend`，点击 **Class Defaults / 类默认值**。
3. 搜索 **Logo Texture**，把刚导入的贴图赋给它，编译、保存。
4. 默认 **Show Logo Text With Texture** 关闭，使用贴图后会隐藏占位文字。如果希望贴图和文字同时显示，再开启它。
5. 进入 Designer 选中 `LogoImage`，调整显示区域大小和位置；重新运行检查淡入、停留、淡出与跳过。

这里真正的运行时贴图入口是 **LogoTexture 类默认值**。只改 `LogoImage → Brush` 可能在运行时被覆盖；LogoTexture 为空时会回到文字占位。

#### 12.3 看懂菜单的五个区域

![图7-2：实际主菜单，编号对应下方说明](assets/FrontendExample/03_MainMenu_annotated.svg)

| 编号 | 实际内容 | 想修改时去哪里 |
|---|---|---|
| ① | 面团世界标题、英文小标题和副标题 | 主标题/副标题先看 `BP_DWFrontendHUD` 的类默认值；字体、颜色、布局在 WBP Designer。保留原控件名 |
| ② | 开始游戏 | 进入原有三个存档槽的页面，继续新建或加载流程；改按钮外观可在 Designer，别重复绑定一次切图 |
| ③ | 设置 | 打开原有设置页，保持语言和其他设置逻辑 |
| ④ | 退出游戏 | 退出运行中的游戏；在编辑器中测试以预览运行状态为准 |
| ⑤ | 房屋、树木、地面等展示场景，带背景模糊 | 去 `L_DWMainMenu_Showcase` 改场景和菜单相机；模糊强度查看 WBP 的 `Menu Blur Strength` |

背景是展示关卡的 **3D 场景画面**，不是把这张截图当一张背景贴图。要换构图，应在场景中调整指定的菜单相机和模型；要让 UI 更清楚，可先小幅调整模糊强度和左侧留白。菜单的轻微镜头运动与整页切换表现见后续对应小节；按钮局部弹跳见第八章。

### 13. 所有页面的统一弹出过渡

新增的统一页面过渡作用于 `PageSwitcher` 切换后真正显示的那一页。点击开始打开存档页、打开设置、设置返回、B 打开背包、Tab 打开制作、Esc 打开暂停，以及进入死亡页，都使用同一组“淡入＋轻微放大回弹＋上浮”参数。旧游戏界面 `WBP_DWGameplay` 和新的 `WBP_DWFrontend` 都继承这个功能。

新 Frontend 的**标题页**继续使用前面 `Anim_MenuEnter` 的专用时间轴，已从通用页面过渡中排除，避免同一页出现两套进入动画。通用设置仍应用于 Frontend 的存档页和设置页。

#### 13.1 修改入口

打开你要修改的主 WBP → Class Defaults → 搜索 `Page Transitions`。游戏内各页改 `WBP_DWGameplay`；独立主菜单里的存档/设置页改 `WBP_DWFrontend`。两个 WBP 的默认值分别保存，修改其中一个不会自动把另一个也改成一样。

| 属性 | 默认值 | 实际含义 |
|---|---:|---|
| `Animate Page Transitions` | true | 是否自动播放通用页面进入效果 |
| `Page Enter Seconds` | 0.32 秒 | 一次过渡总时长，0 时直接显示最终状态 |
| `Page Enter Start Scale` | 0.94 | 相对于该页原始缩放的开始倍率 |
| `Page Enter Offset Y` | 18 | 相对于该页原始位置的初始向下偏移；负数改为从上方进入 |
| `Page Enter Overshoot` | 1.02 | 放大回弹的最高相对倍率；1 表示不放大越过终点 |

示例：想稳重一些，可用 `Seconds=0.30、StartScale=0.98、OffsetY=8、Overshoot=1.005`。想更明显，可以先试 `0.40、0.90、24、1.035`。这些是调节建议，不是另加一套固定美术标准。标题页自己的节奏仍由 `Menu Enter Seconds` 和 `Anim_MenuEnter` 修改。

这套过渡是**参数化的通用 C++ 表现**，不是为每一页新建一条 UMG 动画资产，因此不要在 Animations 列表里找 `Anim_SettingsEnter` 等不存在的轨道。需要逐帧定制某一页时，应由程序为它排除通用过渡，再给它接专用动画，或关闭该 WBP 的通用过渡并明确接管每页流程。

#### 13.2 自动触发与手动节点

- 只有实际从一页切到另一页、或从没有菜单进入某一页时，才自动播放。
- 同一页面刷新背包数量、重新计算配方或更新生命值，不会反复把整页弹一次。
- `Play Current Page Entrance` 可以主动重播当前页的通用进入效果；Frontend 标题仍遵循其专用排除规则。
- `Finish Page Transition` 立即停止当前通用过渡，并还原该页开始前的 Render Opacity 和完整 Render Transform。
- 关闭所有菜单返回游戏，只还原正在过渡的页面并隐藏菜单；这次功能没有额外加入整页退场动画。

快速连续切换页面时，旧页会先恢复，再捕获新页的原始状态。原来在 Designer 设置的自定义位移、旋转、剪切、非 1 缩放和透明度都会保留；刻意设为 0 的透明度也不会被强制变成 1。同页中途重播会先恢复原始状态，不会把尚未结束的 0.97 倍缩放当成下一次基准，因而不会越播越小。

运行中若要用自己的蓝图修改页面根节点的 RenderTransform/Opacity，先调用 `Finish Page Transition`，再设置新值。避免在同一段过渡里让另一个 Tick 或动画同时写同一页根节点。效果使用真实时间推进，暂停菜单也能播放；Designer 静态编辑时不会自动播放。

#### 13.3 与可挂装的 DW UI Bounce 组件配合

本节负责**整页出现**，第 8 章的 `DW UI Bounce` 组件负责**单个按钮或控件的悬停、按压、脉冲、局部弹簧效果**。组件通过独立包装层叠加变换，不直接改这里的页面 RenderTransform，但两种视觉幅度仍会叠加。

已有 `Anim_MenuEnter` 的按钮、或位于通用页面过渡中的控件，建议把组件 `Play On Construct` 关闭，保留其自动 Hover/Pressed 反馈。不要再为每个普通翻页按钮重复调用 `Play Current Page Entrance`；现有 `ApplyMenuPage` 已会在页面变化时自动播放，额外调用可能让同一次进入重新计时。

检查步骤：打开设置→返回→打开存档→返回，观察每页只弹一次；游戏里连续 B/Tab/Esc 切换，确认无残留透明度；编辑某页原始缩放和透明度再反复开关，确认结束后精确恢复。最后检查暂停时也能完成过渡，并确认 Frontend 标题仍使用原有错时进入动画。

### 14. 主菜单背景相机：持续轻摆与翻页轻震

这个效果属于 `BP_DWFrontendHUD`，只影响独立主菜单关卡里选中的运行时 CameraActor。它不修改玩家游玩相机，也不改变 FOV。打开 `BP_DWFrontendHUD → Class Defaults → Frontend | Camera`，下面分为 Sway 和 Transition 两组。

#### 14.1 持续轻摆 Sway

| 参数 | 默认值 | 含义 |
|---|---|---|
| `Enable Camera Sway` | true | 开启背景相机持续的轻微呼吸式运动 |
| `Sway Location Amplitude Cm` | X=0、Y=1.5、Z=1 | 相机局部坐标下的位置最大偏移，单位厘米 |
| `Sway Rotation Amplitude Degrees` | Pitch=0.15、Yaw=0.15、Roll=0.03 | 三方向旋转最大偏移，单位度 |
| `Sway Frequency Hz` | X=0.10、Y=0.08、Z=0.12 | 各方向每秒周期数；数值越小，变化越慢 |

这里的 X 是相机朝前，Y 是相机朝右，Z 是相机朝上。频率的 X/Y/Z 还分别驱动 Pitch/Yaw/Roll 的波形。例如 Y=0.08 Hz 表示约 12.5 秒完成一个左右相关周期。位置数值是相对于摆好的镜头的最大偏移，不是每帧累加的位移。

默认幅度很轻。如果想更稳定，先把位置、旋转幅度减半；想完全静止则关 `Enable Camera Sway`。单纯改变场景构图，仍然要移动关卡里的相机本体，不要靠把摆动幅度调大来补构图。

#### 14.2 页面切换时的短促轻震 Transition

| 参数 | 默认值 | 含义 |
|---|---|---|
| `Enable Transition Nudge` | true | 开启页面变化的短促衰减震动 |
| `Transition Location Amplitude Cm` | X=0、Y=1.8、Z=0.8 | 局部位置脉冲幅度 |
| `Transition Rotation Amplitude Degrees` | Pitch=0.12、Yaw=-0.18、Roll=0.03 | 旋转脉冲幅度 |
| `Transition Frequency Hz` | 3.5 | 短震的振荡频率 |
| `Transition Decay Per Second` | 8 | 衰减速度；越大，越快变轻 |
| `Transition Duration Seconds` | 0.65 秒 | 单次脉冲最大持续时间；0 关闭这次时长 |

HUD 观察菜单页变化：已观察到初始页面后，切到另一个非 None 页面会自动触发，例如标题→设置、设置→标题、标题→存档。初次出现标题不额外算一次“翻页”，同一页面刷新数据也不重复震动。它与第 13 节的 UI 整页弹出可以同时出现：一个动 3D 后景相机，一个动 UI。

`Play Menu Camera Nudge(Strength)` 可主动用默认幅度触发一次；`Add Menu Camera Impulse(LocalLocationAmplitudeCm, LocalRotationAmplitudeDegrees, Strength)` 可以传自定义方向与幅度。Strength 在运行时限制为 -2 到 2，负值反转方向。普通菜单翻页已经自动触发，不要在每个按钮再接一次相同调用。

#### 14.3 保存、暂停和其他相机系统

每帧结果由捕获的原始相机变换加上临时偏移计算，避免长时间运行后镜头不断漂移。切换相机或结束本 HUD 时会处理原有基准的恢复；编辑器世界中的摆放相机不会被这个运行时效果直接写回。菜单暂停时仍按真实时间更新。

想让镜头完全静止，应同时关闭 `Enable Camera Sway` 和 `Enable Transition Nudge`。想用 Level Sequence 接管同一台相机，先关闭这两项，再根据是否切换 ViewTarget 决定关闭 `Keep Menu Camera Active`；不要让 Sequence 和 HUD 同时每帧改同一台相机。

调试时可以读取 `Get Menu Camera Baseline`、`Get Current Menu Camera Offset` 和 `Get Current Menu Camera Rotation`，分别查看基准与当前额外偏移。它们是诊断数据；关卡本身的镜头构图仍通过 CameraActor 的 Transform 编辑。

### 15. 本轮实际验证与仍需替换的占位

本轮编辑器目标 C++ 编译通过，已完成三组 PIE 专项检查。记录中的全部断言均通过：

| 检查记录 | 数量 | 实际覆盖 |
|---|---:|---|
| `FrontendRuntimeQA.json` | 20 项 | 开场到 Ready、跳过、零时长、四条动画绑定、菜单相机、所测按钮组件的 Pulse/悬停/按压/恢复/停止更新、设置返回、三存档显示、原存档文件未变 |
| `MotionRuntimeQA.json` | 34 项 | 暂停时相机轻摆、基准不漂移、关闭后恢复、切页轻震及结束、存档/设置/背包/制作/暂停/死亡六页进入与原始变换恢复、同页不重播、快速切页与存档文件未变 |
| `GamePagesRuntimeQA.json` | 15 项 | 加载已有槽 3 进入原 GameplayPrototype，确认原主角与 `WBP_DWGameplay`，并验证其背包/制作/暂停/设置四页的入场、中间运动及原始外观复原 |

这三组检查验证的是记录中列出的路径；没有把它们扩大为所有控件、所有输入设备或任意改动后的保证。详细证据随文档保存在 `Verification` 文件夹。浏览器文档检查是另一类验证，不替代 UE 运行检查。

当前 **YOUR TEAM / PRESENTS 仍是 Logo 文字占位**。Logo Texture 可由你后续赋图；新增 Logo、菜单揭示、组件悬停和按压音效槽保留供后续美术/音频接入，空槽不会自动生成声音。原有交互音效接口也继续保留，不意味着所有槽已经填入最终音频。

本轮**没有打包独立 EXE，也没有做目标机器 FPS / GPU 性能基准**。使用正式 Logo、声音和场景后，应再按前面的清单检查效果，并在交付目标机器验证打包版本。


## 第三部分 · 可挂载的 UI 弹跳组件（完整手册第 8 章）

本轮组件名为 **DW UI Bounce**，C++ 类型是 `UDWUIBounceComponent`，继承 UE 5.8 的 **UUIComponent**。它添加在 **UMG Designer 选中的控件**上，不是加在场景 Actor 上的 ActorComponent。

效果采用阻尼弹簧，让控件放大、缩小、轻微倾斜或位移后自然回弹，追求类似 TABS 的轻松弹性感觉；这不是 TABS 源码、素材或具体曲线的复刻。

### 1. 先给一个按钮挂上效果

1. 停止游戏，打开你的 WBP，进入 **Designer / 设计器**。
2. 在左侧 **Hierarchy / 层级**选中 **Button 自身**。不要选它里面的 Text，也不要选最外层整个 WBP。
3. 在右侧 Details 的控件名称下点击 **添加组件 / Add Component**，搜索 **DW UI Bounce** 并添加。每个目标控件先只放一份。
4. 选中新加的组件查看参数。先保留 **Auto Bind Buttons** 开启，使用默认参数；它会自动绑定该 Button 的悬停、离开、按下和松开表现，不需要再给同一按钮写一套弹跳事件。
5. 编译、保存 WBP，进入游戏，让这个实际被引用的界面显示出来。
6. 鼠标移上按钮，按钮应略微放大、抬起；按住缩小；松开后回到悬停姿态，移开后回到正常姿态。退出再打开，检查不会残留缩放。

![图8-1：真实UMG界面中，①选择Button，②核对控件名，③点击添加组件](assets/MaterialUIBasics/UI02_AddComponent_annotated.svg)

图8-1标出的是实际 Designer 的添加入口；截图没有伪造组件菜单或参数。选中并添加 DW UI Bounce 后，再在组件详情中调整下文参数。

![图8-2：本工程实际DW UI Bounce参数，重点看右侧六处编号](assets/FrontendExample/04_BounceDesigner_annotated.svg)

在图8-2中，①确认当前目标为 `StartButton`，②确认组件已经添加，③调回弹速度和阻尼，④保留自动按钮事件绑定，⑤调悬停和按压缩放。⑥ **Play on Construct 在这个示例中关闭**，因为主菜单已有 `Anim_MenuEnter` 入场动画；通用组件的初始默认值仍是开启。图中间是 Designer 预览，黑幕和 Logo 图层可能同时显示，不代表最终游戏画面；实际菜单见第七章运行截图。

如果组件列表搜不到它，先确认你在 **UMG 控件的 Details**，而不是 Actor 蓝图的组件面板。还需本轮 C++ 编译后的组件类已经加载；仅复制手册不会给旧工程自动安装类。UE 5.8 提供的 UMG Components 编辑入口属于较新的功能，界面应以当前工程实际显示为准。

添加组件只负责表现，不给按钮增加存档、制作、退出等游戏功能。它绑定的是 `OnHovered / OnUnhovered / OnPressed / OnReleased`，**不接管 OnClicked 业务逻辑**，原有按钮动作继续由原来的流程负责。

### 2. 三种挂法，选择你真正想弹的部分

| 目标 | 怎么挂 | 应注意什么 |
|---|---|---|
| 整个按钮连同文字一起弹 | 挂在 Button 本身 | 最适合第一次使用；自动悬停/按压直接生效 |
| 只有图片或数字弹一下 | 挂在 Image / Text 等目标上，由事件主动 Pulse | 装饰通常不可命中，不要为了悬停把一张全屏装饰图改成会挡鼠标 |
| 整个面板入场 | 挂在外层容器上，播放 Entrance | 内部按钮可再有轻微交互弹跳，但避免父子两层都大幅缩放 |

一个控件的子元素会跟着其父层一起运动。你想只让右上角数量弹，不应把组件挂到整个背包面板。想让整个按钮弹，不要只选中了按钮内文字。

非 Button 控件可通过 **Animate Non Button Hover** 开启悬停响应，前提是其可见性和父层允许鼠标命中。它不会因为是 Image 就自动获得按钮式“按下”业务事件；需要按压表现时，让真实按钮/交互逻辑调用对应节点。

现有 `HUDLayer` 是显示层，装饰控件可能不参与命中。这种情况下给图标调用 Pulse 就可以，不必破坏原来输入层级。

### 3. 最常用参数怎么调

以下默认值来自本轮组件接口。它们是初始配置，不是对所有美术尺寸都合适的统一答案；在自己的界面中调整后要实际试看。

| 字段 | 默认值 | 改变后的感觉 |
|---|---:|---|
| FrequencyHz | 5 | 越高通常回弹越快、越紧；越低更缓慢 |
| DampingRatio | 0.55 | 小于 1 有回弹；等于 1 为临界阻尼；大于 1 缓和趋近目标、不反复弹 |
| Pivot | (0.5, 0.5) | 附加缩放/旋转的支点，默认中心；先保留默认 |
| SettleScaleTolerance | 0.001 | 缩放接近目标时的停止容差，通常不用改 |
| SettlePixelTolerance | 0.05 | 位移停止容差，通常不用改 |
| SettleAngleTolerance | 0.05° | 旋转停止容差，通常不用改 |
| HoverScale | 1.06 | 悬停时额外放大到约 106% |
| PressedScale | 0.94 | 按住时额外缩小到约 94% |
| HoverAngle | -1.5° | 悬停时轻微倾斜 |
| PressedAngle | 1.5° | 按住时反方向轻微倾斜 |
| HoverOffset | (0, -2) | 悬停时向上少量移动 |
| PressedOffset | (0, 1) | 按住时向下少量移动 |
| EntryScale | 0.82 | 入场起始缩放 |
| EntryOffset | (0, 30) | 入场起始偏移，正 Y 向下 |
| EntryAngle | -3° | 入场起始角度 |
| PulseScaleOffset | 0.08 | 主动 Pulse 的额外缩放幅度设置 |
| PulseOffset | (0, -8) | 主动 Pulse 的位移设置 |
| PulseAngle | 3° | 主动 Pulse 的角度设置 |
| Play On Construct / bPlayOnConstruct | 开启 | 控件构造显示时自动入场 |
| Auto Bind Buttons / bAutoBindButtons | 开启 | 自动接 Button 的悬停和按压 |
| Animate Non Button Hover / bAnimateNonButtonHover | 开启 | 非 Button 控件可响应悬停，仍受命中设置影响 |
| HoverSound | 空 | 可选的悬停音效，不填则不额外播放 |
| PressedSound | 空 | 可选的按下音效，不替代原 OnClicked 点击音效 |
| SoundVolume | 0.35 | 本组件可选音效的播放音量，可调范围 0～2 |

Scale 是额外倍率，1 表示不额外缩放；Offset 是 UI 偏移，不是场景厘米；Angle 是角度。若美术本身已经很倾斜，或观察时容易眩晕，先把各个 Angle 调到 0，再减少 Offset 和缩放幅度。减少旋转通常比把整个弹跳完全关掉更容易保留清晰的反馈。

建议按顺序调：**先幅度 → 再频率 → 最后阻尼**。一次只改一组。不要同时把放大、旋转、上下位移和回弹时间都拉大，否则难以知道是哪一项不舒服。

已有点击声时先让组件音效槽保持空。需要悬停声再填 HoverSound；填 PressedSound 后，一次点击可能先听见按下声，再听见原按钮的点击声，这是两套反馈叠加。请实际听过再决定，避免把每个槽都填成同一个很响的声音。

### 4. 让合成成功、数量变化等事件主动弹一下

自动按钮反馈适合鼠标操作；获得物品、合成完成或属性变化时，用手动触发。

#### 4.1 最简单的蓝图节点：Pulse Widget

1. 在 Designer 给目标 Image / Text / 容器添加 DW UI Bounce。
2. 若要在 Graph 引用该控件，勾选 **Is Variable / 是否变量**，编译。
3. 切到 Graph，从 My Blueprint 拖出这个控件的引用，选择 Get。
4. 搜索 **Pulse Widget**，把目标控件引用接到它的 Widget 输入。
5. 把真实反馈事件的白色执行线连进去；Strength 默认 1，可先使用较小值试看。
6. 检查函数返回结果。目标没有挂组件时会返回 false；helper 不会悄悄创建组件，也不修改资产。返回 true 只表示找到了组件并调用了它，不证明画面上已经产生运动：控件尚未构造或处于 Designer 时，动画会被安全跳过。

例如已有制作反馈应从 `OnRecipeCraftResult` 判断成功后播放一次，不再额外调用一次制作。物品数量效果应在确认数量发生变化时触发，不要把 Pulse 接到每一帧的数值刷新。

#### 4.2 需要更多控制时取得组件

使用 **Get Bounce Component**，传入目标 Widget，取得这一个控件已经挂载的组件；未挂时返回空，应先检查有效性。

| 组件节点 | 用途 |
|---|---|
| Pulse(Strength) | 主动弹一次，默认 Strength=1 |
| Play Entrance | 从组件配置的入场姿态开始回弹 |
| Set Hovered(true/false) | 由自定义导航或交互逻辑控制悬停目标 |
| Set Pressed(true/false) | 由自定义逻辑控制按住目标；结束时要回 false |
| Reset Bounce | 停止本组件运动、清除 Hover/Pressed，并恢复附加变换为正常值 |
| Is Animating | 检查本组件是否还在运动 |
| Get Current Scale / Angle / Offset | 检查本组件的附加变换，不是整个屏幕上最终合成的总变换 |

不需要取得组件也可使用 **Play Widget Entrance** 和 **Reset Widget Bounce** helper。它们同样只操作目标上已经存在的组件，不会自动添加。

### 5. “第一次有动画，再打开怎么没有”

`Play On Construct` 对 Button、Image 和容器都适用。但**控件构造**与**页面从隐藏变可见**不是同一件事。

本项目 `PageSwitcher` 可能已经把所有页面构造好，只是在切换显示。按钮的入场可能在页面还看不见时已经播放，之后打开该页不会自然重播。

1. 如果希望每次打开页面都入场，在页面切换通知中判断进入了目标页。
2. 把该页需要弹出的容器引用传给 **Play Widget Entrance**。
3. 如果统一由这个事件管理入场，可以关闭该容器组件的 Play On Construct，避免构造时多播一遍。
4. 不要在每帧 `OnHUDValuesChanged` 里触发 Entrance；否则动画会一直重新开始。

现有主 WBP 有 `OnMenuPageChanged` 通知；独立新页面应使用它自己的“刚打开”事件。具体页面流程和开场动画接线见开场与主菜单示例章节。

### 6. 与 UMG 时间轴怎样配合

| 需要做的效果 | 更适合交给谁 |
|---|---|
| Logo 淡入、等待、淡出，镜头到菜单的整段流程 | UMG Animation / 页面流程 |
| 一排元素错开入场、需要精确关键帧的编排 | UMG Animation |
| Button 悬停、按下、松开回弹 | DW UI Bounce 自动绑定 |
| 获得物品后图标局部弹一下 | DW UI Bounce 的 Pulse |
| 播完动画再切页或关闭菜单 | 页面业务逻辑等待完成，不由弹跳组件替代 |

组件不改现有控件的 UMG Render Transform，而是在自己的 Slate 包装层上叠加额外缩放、偏移和旋转，所以可以与原有 UMG 动画组合。**能组合不代表应该叠很大幅度**：父容器放大、按钮再放大、按钮文字再放大，会把总效果一起放大。通常让面板负责一次入场，按钮负责轻微交互就足够。

本轮 `WBP_DWFrontend` 的 `TitlePage` 和三个主按钮已经由 `Anim_MenuEnter` 编排；给这些控件挂 DW UI Bounce 时，建议 **关闭 Play on Construct**，只保留自动 Hover/Pressed。现有统一页面过渡负责切页入场，参数见第七章；它和组件效果也会叠加，不需要再让每个按钮都额外播放一次 Entrance。确实需要额外入场时，再在明确的页面事件里调用 Play Widget Entrance。

`Reset Bounce` 只重置组件这一层；如果原有 UMG 动画还在播放，它仍会继续。这是各自独立的效果，不是 Reset 失效。调试 getter 的 Scale 也仅是组件额外倍率，不应拿它直接当作最终屏幕尺寸。

### 7. 键盘、手柄与暂停

按钮正常激活带来的 Pressed/Released 事件也可触发相应回弹。但**键盘或手柄仅把焦点移动到一个按钮，并不自动等于鼠标 Hover**。要让焦点切换也放大，可以在自己的导航焦点变化流程中调用该按钮组件的 `Set Hovered(true)`，焦点离开时配对调用 false。

不要为了加效果给旧按钮再执行一遍 OnClicked，也不要在输入未释放时只调用 Set Pressed(true) 而忘记恢复。

组件按真实时间推进，适合暂停菜单仍继续运动。它不会替你暂停世界、隐藏鼠标或决定游戏输入模式；这些仍由 HUD/Player Controller 管理。

### 8. 性能和使用数量

实现使用按需注册的真实时间更新：只有运动中需要逐步计算，状态稳定后注销本组件的更新；控件销毁时清理自己的绑定和更新。它通过附加绘制变换产生效果，不每帧修改 Anchors、Canvas Slot 位置或控件布局尺寸。

这并不意味着零成本：运动时仍有变换和绘制更新，大量控件同时反复 Pulse 也会产生开销。这里说明的是实现方式，**没有进行目标机器帧率或 GPU 开销基准测试**。

日常使用建议：

- 先给少数主要按钮和关键反馈加效果，再决定是否扩展。
- 一整组元素一起入场时优先给外层容器挂一份，避免给每层都挂同样的大动画。
- 列表中不用的每个格子无需持续循环弹跳。只在新获得、选中或重要状态变化时触发。
- 不把 Pulse/Entrance 接到 Tick，也不不断重复设置相同状态来强行维持运动。
- 同一按钮保留一套自动交互绑定；外部要完全控制状态时再关闭 Auto Bind Buttons。

### 9. 常见问题

| 现象 | 检查 |
|---|---|
| 添加组件里找不到 DW UI Bounce | 是否在 UMG 控件 Details；本轮类是否已编译并加载 |
| 悬停没反应 | 选中的是否是 Button；Auto Bind Buttons 是否开启；控件及父层是否允许命中；按钮是否被禁用/覆盖 |
| 图片不响应鼠标 | 装饰可能不可命中，这是正常设置；使用主动 Pulse，不要让全屏装饰去挡按钮 |
| Pulse Widget 返回 false | 传入的控件不是那一个已挂组件的目标，或根本没挂组件 |
| 返回 true 但没看到弹跳 | true 只说明找到组件并调用；检查是否仍在 Designer、尚未构造、隐藏页或被上层遮住 |
| 只在第一次有入场 | Construct 不等于每次显示；切页时主动 Play Widget Entrance |
| 点击游戏功能执行两遍 | 检查你自己是否重复绑定业务动作；组件没有接管 OnClicked |
| 按下状态卡住 | 自定义 Set Pressed 是否漏掉 false；需要时调用 Reset Bounce |
| 一直在抖 | 是否每帧 Pulse、阻尼过低、频繁重启入场，或多层大幅叠加 |
| 重置后还在动 | 可能是原有 UMG 动画仍在播放，Reset 只控制本组件层 |
| 返回Scale是1，但画面仍放大 | getter 只报告组件附加倍率；查看控件自身和父层的 UMG 变换 |

本轮组件已随编辑器目标编译通过。`FrontendRuntimeQA.json` 的 20 项检查中包含所测菜单按钮的 Pulse 非零位移、回稳后停止更新、悬停到 1.06、按住到 0.94、松开回悬停和移开回正常状态，相关断言通过。测试范围不等于所有控件类型或输入设备均已逐一实测；未做独立 EXE 打包及 FPS / GPU 基准。新增 HoverSound / PressedSound 仍为空槽，等待你填入正式音效。


## 附录 · 材质与遮挡截图实操（完整手册第 5 章）

本章先教你找到并更换材质，再用已经修好的 `BP_BLD_house_4` 演示“房屋挡住玩家时淡出”。截图来自当前 UE 工程，彩色框和编号只是额外标注，原界面和数值没有改画。看不清时打开 [截图放大指引](assets/MaterialUIBasics/截图放大指引.html)，点击大图；其中也保留了未标注原图。

### 1. 先分清四种东西

| 名称 | 在这个工程中是什么 | 日常需要做什么 |
|---|---|---|
| Texture / 贴图，通常以 `T_` 开头 | 一张颜色图、法线图或遮罩图 | 双击检查图片；必要时重新导入 |
| Material / 母材质，通常以 `M_` 开头 | 决定贴图和参数怎样计算、最终怎样显示的节点图 | 只有要改变计算方式时才修改 |
| Material Instance / 材质实例，通常以 `MI_` 开头 | 继承母材质，保存这一个外观的贴图和参数 | 换木材颜色、墙面贴图、透明模式等通常从这里改 |
| Mesh 的 Material Slot / 材质槽 | 模型的某部分实际使用哪一个材质 | 在模型或蓝图网格组件上分配正确的实例 |

前缀是命名习惯，不是功能开关。把名字改成 `_CameraFade` 不会自动得到淡出功能。运行时生成的 **Dynamic Material Instance / 动态材质实例**是程序临时控制参数的对象；停止游戏后应回到保存的材质实例，不要把它当成需要保存的正式资源。

本项目的关系是：**母材质计算 → 材质实例保存外观 → 房屋网格的各个材质槽引用实例 → 淡出组件在运行时调整参数。**

### 2. 第一次更换房屋材质，按这六步做

1. 停止 Play。在关卡中选中房屋，点击“编辑蓝图 / Edit Blueprint”。找不到该房屋时，在内容浏览器依次打开 `Content → DoughWorld → Fantastic_Village_Pack → blueprints → buildings`，双击 `BP_BLD_house_4`。
2. 在蓝图左侧“组件 / Components”点击 `SM_BLD_body_v05_01`。不要选最上面的“自身”，也不要选灯光组件。
3. 在右侧“细节 / Details”的搜索栏输入 `Materials`，展开材质槽。
4. 看清每一项：同一主体有木材、墙、屋顶、石砖等多个槽。换其中一个槽只影响对应部分，不能把所有槽都换成同一张木材。
5. 点击槽位右侧资源选择下拉框，搜索并选择需要的实例；也可以先在内容浏览器选好实例，再用槽位旁“使用当前选中资产”的按钮。放大镜用于在内容浏览器定位当前资源，不是替换按钮。
6. 编译并保存蓝图，回到关卡运行检查。只在蓝图窗口改完但不保存，不代表下次打开工程还保留。

![图5-1：①选择网格，②搜索Materials，③分别指定四个材质槽](assets/MaterialUIBasics/04_MaterialSlot_annotated.svg)

**改在哪里，影响范围不同：**在静态网格资产中修改默认槽，会影响继承该默认值的使用者；在房屋蓝图组件中改槽，会影响使用该蓝图默认值的房屋；在关卡中某一个房屋实例上覆盖，只影响这个实例。若蓝图改了但某一栋房屋没变，先检查那栋房屋是否保存着自己的槽位覆盖，不要直接重置整栋房屋。

### 3. 修改一个材质实例的外观

1. 从刚才的槽位定位并双击一个 `MI_` 资源。如果打开后看到的是大片节点图，说明你打开了母材质；返回找到对应实例。
2. 实例详情中，每个可覆盖参数左侧有勾选框。**先勾选，再改值**，表示这个实例覆盖父级的该项参数；不勾选就继续继承父级。
3. 颜色参数打开颜色选择器；纹理参数选择另一张贴图；标量参数输入数值。参数名称取决于作者的母材质，不同资产包不一定叫 `Color` 或 `Roughness`。
4. 一次改一项，观察预览。想恢复这一项，可以取消它的覆盖或使用该属性的重置箭头；别把整个实例所有参数一起清空。
5. 保存实例，回关卡检查真实光照下的效果。实例预览球上的纹理比例与模型 UV 不一定相同。

若只想做一栋不同颜色的房屋，先在内容浏览器右键实例 → Duplicate / 复制，再改副本并分配给那栋房屋。直接修改共享实例，会影响其他正在引用它的房屋。

普通颜色图一般接 Base Color，法线图接 Normal，黑白数据图可能接 Roughness 或其他遮罩。不要把颜色图填到 Normal，或把法线图当 UI 图标。换贴图时对照原槽位用途；出现紫色、反光异常或表面凹凸倒置时先检查这一点。

### 4. 房屋淡出需要三件事同时成立

| 必需项 | 负责什么 | 单独具备能否生效 |
|---|---|---|
| Actor 上的 `CameraOccluderFade` 组件 | 检测遮挡，逐步写入参数 | 不能；还要找到目标网格和兼容材质 |
| 目标网格的 Component Tags 含 `CameraFade` | 标记哪些网格允许淡出 | 不能；标签不是材质效果 |
| 接好遮罩的兼容材质实例 | 真正让像素按参数淡出 | 还需要正确实例覆盖和前两项 |

你此前的标签大部分已经填对了。最初 34 个网格中仅房屋主体漏标，另外 13 个实例没有兼容参数。配置补齐后仍出现屋顶实心，进一步发现了材质的有效混合模式问题；**最终的关键设置在第 7 节：13 个兼容实例明确勾选 Blend Mode 覆盖为 Masked。**

### 5. 添加组件与设置网格标签

#### 5.1 在 Actor 蓝图添加或选中淡出组件

打开 `BP_BLD_house_4` 后，左侧已经有 `CameraOccluderFade`，直接选它，不要再加一份。如果是你自己的新 Actor 蓝图，点击左上“添加组件”，搜索 `Camera Occluder Fade` 添加一次。

按图中编号检查：

1. 选中左侧的淡出组件。
2. `Component Tag` 与 `Parameter Name` 都填写 `CameraFade`。
3. 勾选 `Protect Player`，`Local Player Index` 为当前单人玩家 `0`。`Occluded Visibility` 越小，遮挡时越不明显；当前使用 `0.2`。组件应启用 `Auto Activate`。
4. `Fade Out Seconds`、`Fade In Seconds` 分别调整淡出和恢复用时，数值越大变化越慢；`Clear Hold Seconds` 是离开遮挡后的短暂保持时间。

![图5-2：组件选择、标签/参数名、保护玩家和渐变时长](assets/MaterialUIBasics/01_Component_annotated.svg)

#### 5.2 在每个网格上加标签

1. 左侧选 `SM_BLD_body_v05_01`，右侧搜索 **Component Tags**。
2. 点击数组旁 `+`，在新的一项填 `CameraFade`。已有就不用重复。
3. 对需要淡出的屋顶、墙、门重复；不需要淡出的网格可以不加。
4. 编译、保存。

![图5-3：网格的Component Tags位置](assets/MaterialUIBasics/02_MeshTag_annotated.svg)

标签加在**网格组件**上，不是 Actor Tags，也不是淡出组件自身的标签数组。父网格的标签不会自动传给下面的子网格。

### 6. 母材质怎样获得淡出能力

**已经存在 `_CameraFade` 副本时，优先复用，不必重新搭图。** 本批兼容资源位于：

```text
/Game/DoughWorld/Maps/Gameplay/Materials/CameraFade/FantasticVillage/
  Masters/    兼容母材质
  Instances/  兼容材质实例
```

要学习节点，打开 `Masters/M_Master_opaque_CameraFade`。图中的四个框分别是：

1. `CameraFade`：Scalar Parameter / 标量参数，默认值 `1`。
2. 参数连到 `DitherTemporalAA` 的 `Alpha Threshold` 输入。
3. 函数结果连到最终材质节点的 **Opacity Mask / 不透明蒙版**。
4. 母材质的 `Blend Mode` 配置为 **Masked**。

![图5-4：真实材质图中的参数、函数、输出和Masked位置](assets/MaterialUIBasics/03_MaterialGraph_annotated.svg)

如果是另一个资产包的普通不透明母材质，先复制母材质，再在副本中增加以上参数与连接。右键空白处搜索节点，按住输出引脚拖到输入引脚连接；不要删除原来的 Base Color、Normal、Roughness 线。连接后点击 Apply / 应用并保存，再制作对应实例。仅摆出一个没有连接到输出的 `CameraFade` 节点不起作用。

原材质类型不同，接法也不同：

| 类型 | 应保留和连接的内容 |
|---|---|
| Opaque / 不透明墙和屋顶 | 兼容副本改 Masked，`DitherTemporalAA(CameraFade)` 接 Opacity Mask |
| 已有 Masked / 镂空叶片 | 原有叶片遮罩乘以 `DitherTemporalAA(CameraFade)`，再接 Opacity Mask；不能抹掉叶片原来的镂空 |
| 真正使用 Translucent / 半透明玻璃 | 保持 Translucent，让“原 Opacity × CameraFade”接 Opacity；没有原 Opacity 输入时以 1 为原值 |

这是普通 Surface 材质图的接法。使用 Material Attributes 的材质要在对应属性中合并遮罩，不能随意替换整组属性。UI 材质属于另一种用途，不把这个房屋 Surface 材质直接拿去当 UMG 背景。

本房屋 CLR 发光材质是已处理的特例：原实例 `MI_CLR_emission_yellow` 的父材质是 Translucent，但实例强制覆盖为 Opaque。**当前 CLR 兼容母材质已经改为 Masked + DitherTemporalAA → Opacity Mask，对应兼容实例也显式覆盖为 Masked。** 不要把它当作通用 Translucent 示例。

### 7. 最关键的界面：实例勾选 Blend Mode → Masked

这一步解决了“组件判定遮挡、参数也下降，但屋顶仍然实心”的问题。只把母材质面板改成 Masked 不够。

1. 在 `Instances` 文件夹打开当前材质实例。图中示例是屋顶实例；木材可以打开 `MI_wood_05_CameraFade`。
2. 在右侧细节搜索 `Blend Mode`，展开 **Material Property Overrides / 材质属性重载（或材质属性覆盖）**。
3. **勾选 Blend Mode 左侧小方框**。
4. 在下拉框明确选择 **Masked**，保存。不要取消覆盖，也不要选回 Opaque。

![图5-5：①打开MI，②搜索Blend Mode，③勾选覆盖，④选择Masked](assets/MaterialUIBasics/07_InstanceBlendOverride_annotated.svg)

本批 13 个兼容实例已经全部完成这个设置。之后制作新外观，优先复制对应兼容实例，再替换贴图和外观参数。若复制原资产实例后再换 Parent，必须确认 Parent 是对应兼容母材质，并明确打开上述 Masked 覆盖。

例如木材的完整实例路径：

`/Game/DoughWorld/Maps/Gameplay/Materials/CameraFade/FantasticVillage/Instances/MI_wood_05_CameraFade`

**此开关不能替代第 6 节的有效遮罩连接。** 未接 `CameraFade` 的普通母材质，只勾 Masked 也不会获得遮挡淡出；真正需要半透明的玻璃不能机械地改用这一模式。

技术原因供程序对接：旧包的 `bCanMaskedBeAssumedOpaque` 仍为 True，没有被清除。最终通过实例的 `override_blend_mode=True`、`blend_mode=Masked` 绕过父材质的有效 Opaque 状态。无需你修改隐藏属性或写 C++；原资产包的材质保持不变。

### 8. 分配实例并在真实玩家视角验收

1. 回蓝图，按第 2 节为每个被标记网格的各个槽分配对应 `_CameraFade` 实例。
2. 编译、保存，进入能控制玩家的 Play。
3. 走到房屋后，让游戏摄像机到玩家的视线穿过房屋。
4. 看实际画面：遮挡部分出现抖动淡出，玩家能被看见。Masked 抖动可能有细点，并不是玻璃一样连续半透明。
5. 离开遮挡，观察它恢复实心。换一个视角再检查屋顶、墙与装饰是否有漏换的材质槽。

![图5-6：本工程真实运行画面，屋顶抖动淡出后角色可见](assets/MaterialUIBasics/originals/06_Actual_After.png)

这次真实同机位验证已看见角色；退出遮挡后 82 个已管理槽的数值恢复为 1、遮挡状态为 false。此前临时保护目标验证的 `1 → 0.2 → 1` 只证明检测和参数写入，不能单独证明画面透明；后续验收应同时看画面与状态。没有宣称所有资产、所有视角都已经逐一检查。

### 9. 没效果时怎样定位

| 看到的现象 | 应做的检查 |
|---|---|
| 编辑器中绕着房屋看，没有变化 | 正常；组件在运行世界工作。进入有玩家和游戏摄像机的 Play |
| 部分屋顶/墙不淡出 | 逐个检查 Component Tags，以及该网格所有材质槽是否都用了兼容实例 |
| 参数下降，画面仍实心 | 检查图5-5的实例覆盖是否勾选 Masked、图5-4的 Opacity Mask 是否接线；不要只看母材质显示的模式 |
| 新副本外观变成另一种木头/墙 | Parent 是否换错，贴图覆盖是否保留，材质槽是否填错 |
| 切换材质后突然不受控制 | 运行时替换后调用淡出组件的 `Refresh Fade Meshes` 一次，不要每帧调用 |
| 整栋大范围一起淡出 | 当前按网格组件包围盒判断；把屋顶/上墙等拆为独立组件可细化范围 |
| 草或实例化树木不工作 | 当前组件跳过 ISM/HISM；植被系统需要专门的逐实例方案，不能只复制普通网格步骤 |

程序排查节点可从蓝图中的 `CameraOccluderFade` 组件引用拖线搜索：`Get Managed Mesh Count`、`Get Skipped Slot Count`、`Evaluate Occlusion Now`、`Get Fade Status`、`Get View Snapshot`。输出日志中的 `lacks scalar CameraFade` 说明槽位不兼容；`Skipping ISM/HISM` 说明组件类型不支持。

本实现只扫描同一个 Actor 内的普通 StaticMeshComponent；Child Actor 有自己的 Owner，需单独配置。它依据视线与包围盒检测，不靠碰撞射线通道，因此不必关闭房屋碰撞来“修复透明”。

本章依据：当前 `CameraOccluderFadeComponent` 接口、专项使用说明，以及实际 `ExplicitMaskedFix.json`、`VisualRepairResult.json`、`VisualRestored.json` 记录。后续修改会影响结果，应按第 8 节重验。
