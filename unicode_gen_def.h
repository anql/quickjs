/**
 * @file unicode_gen_def.h
 * @brief Unicode 通用类别、脚本和属性定义表
 * 
 * 本文件是 Unicode 表生成器 (unicode_gen.c) 使用的数据定义文件。
 * 通过条件编译宏，定义了三类 Unicode 元数据：
 * 
 * 1. UNICODE_GENERAL_CATEGORY: Unicode 通用类别（General Category）
 *    - 用于字符分类，如字母、数字、标点、符号等
 *    - 每个类别有缩写（如 "Lu"）和全称（如 "Uppercase_Letter"）
 *    - 类别遵循 Unicode 标准规范，参见 Unicode Character Database
 * 
 * 2. UNICODE_SCRIPT: Unicode 文字/脚本（Script）
 *    - 定义字符所属的书写系统，如拉丁文、汉字、阿拉伯文等
 *    - 每个脚本有全称（如 "Han"）和缩写（如 "Hani"）
 *    - 用于正则表达式中的 \p{Script=...} 语法
 * 
 * 3. UNICODE_PROP_LIST: Unicode 二元属性列表
 *    - 定义字符的各种布尔属性，如 ASCII、Alphabetic、Emoji 等
 *    - 部分属性仅内部使用（带 1 后缀），部分导出到 JavaScript
 * 
 * 4. UNICODE_SEQUENCE_PROP_LIST: Unicode 序列属性
 *    - 定义由多个码点组成的序列属性，如 Emoji 序列
 * 
 * @note 本文件通过 DEF 宏被 unicode_gen.c 多次包含，每次定义不同的内容
 * @see unicode_gen.c - Unicode 表生成器主程序
 */

#ifdef UNICODE_GENERAL_CATEGORY
/**
 * @defgroup Unicode_General_Category Unicode 通用类别
 * 
 * Unicode 通用类别是字符分类的核心机制，每个字符恰好属于一个类别。
 * 类别代码由两个字母组成（如 "Lu"），遵循 Unicode 标准。
 * 
 * 类别分组：
 * - Letter (L): Lu, Ll, Lt, Lm, Lo
 * - Mark (M): Mn, Mc, Me
 * - Number (N): Nd, Nl, No
 * - Symbol (S): Sm, Sc, Sk, So
 * - Punctuation (P): Pc, Pd, Ps, Pe, Pi, Pf, Po
 * - Separator (Z): Zs, Zl, Zp
 * - Other (C): Cc, Cf, Cs, Co, Cn
 * 
 * @{
 */

/** @brief 未分配字符 (Unassigned) - 必须为 0，作为默认值 */
DEF(Cn, "Unassigned") /* must be zero */
/** @brief 大写字母 (Uppercase Letter) - 如 A-Z */
DEF(Lu, "Uppercase_Letter")
/** @brief 小写字母 (Lowercase Letter) - 如 a-z */
DEF(Ll, "Lowercase_Letter")
/** @brief 首字母大写字母 (Titlecase Letter) - 如 Đ (DŽ 的标题形式) */
DEF(Lt, "Titlecase_Letter")
/** @brief 修饰字母 (Modifier Letter) - 用于语音学符号等 */
DEF(Lm, "Modifier_Letter")
/** @brief 其他字母 (Other Letter) - 不属于上述类别的字母 */
DEF(Lo, "Other_Letter")

/** @brief 非间距标记 (Nonspacing Mark) - 附加符号，如重音符号 */
DEF(Mn, "Nonspacing_Mark")
/** @brief 间距标记 (Spacing Mark) - 占据空间的标记 */
DEF(Mc, "Spacing_Mark")
/** @brief 包围标记 (Enclosing Mark) - 包围字符的标记 */
DEF(Me, "Enclosing_Mark")

/** @brief 十进制数字 (Decimal Number) - 如 0-9 */
DEF(Nd, "Decimal_Number,digit")
/** @brief 字母数字 (Letter Number) - 如罗马数字 Ⅰ, Ⅱ */
DEF(Nl, "Letter_Number")
/** @brief 其他数字 (Other Number) - 如分数 ½ */
DEF(No, "Other_Number")

/** @brief 数学符号 (Math Symbol) - 如 +, ×, ÷ */
DEF(Sm, "Math_Symbol")
/** @brief 货币符号 (Currency Symbol) - 如 $, €, ¥ */
DEF(Sc, "Currency_Symbol")
/** @brief 修饰符号 (Modifier Symbol) - 如 ˆ, ˜ */
DEF(Sk, "Modifier_Symbol")
/** @brief 其他符号 (Other Symbol) - 如 ©, ® */
DEF(So, "Other_Symbol")

/** @brief 连接标点 (Connector Punctuation) - 如下划线 _ */
DEF(Pc, "Connector_Punctuation")
/** @brief 破折号标点 (Dash Punctuation) - 如 -, — */
DEF(Pd, "Dash_Punctuation")
/** @brief 开标点 (Open Punctuation) - 如 (, [, { */
DEF(Ps, "Open_Punctuation")
/** @brief 闭标点 (Close Punctuation) - 如 ), ], } */
DEF(Pe, "Close_Punctuation")
/** @brief 起始引号 (Initial Punctuation) - 如 ", ' */
DEF(Pi, "Initial_Punctuation")
/** @brief 结束引号 (Final Punctuation) - 如 ", ' */
DEF(Pf, "Final_Punctuation")
/** @brief 其他标点 (Other Punctuation) - 如 ., !, ? */
DEF(Po, "Other_Punctuation")

/** @brief 空格分隔符 (Space Separator) - 如普通空格 */
DEF(Zs, "Space_Separator")
/** @brief 行分隔符 (Line Separator) - U+2028 */
DEF(Zl, "Line_Separator")
/** @brief 段落分隔符 (Paragraph Separator) - U+2029 */
DEF(Zp, "Paragraph_Separator")

/** @brief 控制字符 (Control) - ASCII 控制字符 */
DEF(Cc, "Control,cntrl")
/** @brief 格式控制符 (Format) - 如零宽空格 */
DEF(Cf, "Format")
/** @brief 代理对 (Surrogate) - UTF-16 代理码点 */
DEF(Cs, "Surrogate")
/** @brief 私用区 (Private Use) - 用户自定义字符 */
DEF(Co, "Private_Use")

/**
 * @}
 */

/**
 * @defgroup Synthetic_Properties 合成属性
 * @brief 由多个基本类别组合而成的派生类别
 * @{
 */

/** @brief 有大小写的字母 (Cased Letter) - Lu + Ll + Lt */
DEF(LC, "Cased_Letter")
/** @brief 所有字母 (Letter) - Lu + Ll + Lt + Lm + Lo + LC */
DEF(L, "Letter")
/** @brief 所有标记 (Mark) - Mn + Mc + Me */
DEF(M, "Mark,Combining_Mark")
/** @brief 所有数字 (Number) - Nd + Nl + No */
DEF(N, "Number")
/** @brief 所有符号 (Symbol) - Sm + Sc + Sk + So */
DEF(S, "Symbol")
/** @brief 所有标点 (Punctuation) - Pc + Pd + Ps + Pe + Pi + Pf + Po */
DEF(P, "Punctuation,punct")
/** @brief 所有分隔符 (Separator) - Zs + Zl + Zp */
DEF(Z, "Separator")
/** @brief 所有其他字符 (Other) - Cc + Cf + Cs + Co + Cn */
DEF(C, "Other")
/**
 * @}
 */

#endif /* UNICODE_GENERAL_CATEGORY */

#ifdef UNICODE_SCRIPT
/**
 * @defgroup Unicode_Script Unicode 文字/脚本
 * 
 * Unicode 脚本定义字符所属的书写系统。每个字符属于一个且仅一个脚本。
 * 脚本名称遵循 Unicode PropertyValueAliases.txt 规范。
 * 
 * 脚本用途：
 * - 正则表达式中的 \p{Script=Han} 语法
 * - 文本处理和排版
 * - 语言识别和字体选择
 * 
 * 特殊脚本：
 * - Common (Zyyy): 多脚本共享字符（如标点、数字）
 * - Inherited (Zinh): 从上下文继承脚本的字符（如组合标记）
 * - Unknown (Zzzz): 未分配或私有码点
 * 
 * @{
 */

/** @brief 未知脚本 (Unknown) - 默认值，用于未分配码点 */
DEF(Unknown, "Zzzz")
/** @brief 阿德拉姆文 (Adlam) - 西非富拉尼语文字 */
DEF(Adlam, "Adlm")
/** @brief 阿洪姆文 (Ahom) - 印度阿萨姆邦古文字 */
DEF(Ahom, "Ahom")
/** @brief 安纳托利亚象形文字 (Anatolian Hieroglyphs) - 古赫梯象形文字 */
DEF(Anatolian_Hieroglyphs, "Hluw")
/** @brief 阿拉伯文 (Arabic) - 阿拉伯语、波斯语等使用的文字 */
DEF(Arabic, "Arab")
/** @brief 亚美尼亚文 (Armenian) - 亚美尼亚语文字 */
DEF(Armenian, "Armn")
/** @brief 阿维斯塔文 (Avestan) - 古波斯祆教经文文字 */
DEF(Avestan, "Avst")
/** @brief 巴厘文 (Balinese) - 印度尼西亚巴厘岛文字 */
DEF(Balinese, "Bali")
/** @brief 巴穆姆文 (Bamum) - 喀麦隆巴穆姆语文字 */
DEF(Bamum, "Bamu")
/** @brief 巴萨文 (Bassa Vah) - 利比里亚巴萨语文字 */
DEF(Bassa_Vah, "Bass")
/** @brief 巴塔克文 (Batak) - 印度尼西亚苏门答腊文字 */
DEF(Batak, "Batk")
/** @brief 贝里亚文 (Beria Erfe) - 乍得贝里亚语文字 */
DEF(Beria_Erfe, "Berf")
/** @brief 孟加拉文 (Bengali) - 孟加拉语、阿萨姆语文字 */
DEF(Bengali, "Beng")
/** @brief 拜克苏基文 (Bhaiksuki) - 印度古文字 */
DEF(Bhaiksuki, "Bhks")
/** @brief 注音符号 (Bopomofo) - 汉语注音系统 */
DEF(Bopomofo, "Bopo")
/** @brief 婆罗米文 (Brahmi) - 印度古文字，多种文字的祖先 */
DEF(Brahmi, "Brah")
/** @brief 盲文 (Braille) - 触觉文字系统 */
DEF(Braille, "Brai")
/** @brief 布吉文 (Buginese) - 印度尼西亚苏拉威西文字 */
DEF(Buginese, "Bugi")
/** @brief 布希德文 (Buhid) - 菲律宾民都洛岛文字 */
DEF(Buhid, "Buhd")
/** @brief 加拿大原住民音节文字 (Canadian Aboriginal) - 克里语等原住民语言 */
DEF(Canadian_Aboriginal, "Cans")
/** @brief 卡里亚文 (Carian) - 古安纳托利亚文字 */
DEF(Carian, "Cari")
/** @brief 高加索阿尔巴尼亚文 (Caucasian Albanian) - 古高加索文字 */
DEF(Caucasian_Albanian, "Aghb")
/** @brief 查克玛文 (Chakma) - 孟加拉国查克玛语文字 */
DEF(Chakma, "Cakm")
/** @brief 占文 (Cham) - 越南占族文字 */
DEF(Cham, "Cham")
/** @brief 切罗基文 (Cherokee) - 北美切罗基语文字 */
DEF(Cherokee, "Cher")
/** @brief 花剌子模文 (Chorasmian) - 古中亚文字 */
DEF(Chorasmian, "Chrs")
/** @brief 通用字符 (Common) - 多脚本共享的字符（标点、数字等） */
DEF(Common, "Zyyy")
/** @brief 科普特文 (Coptic) - 埃及科普特语文字 */
DEF(Coptic, "Copt,Qaac")
/** @brief 楔形文字 (Cuneiform) - 古代美索不达米亚文字 */
DEF(Cuneiform, "Xsux")
/** @brief 塞浦路斯音节文字 (Cypriot) - 古希腊塞浦路斯文字 */
DEF(Cypriot, "Cprt")
/** @brief 西里尔文 (Cyrillic) - 俄语、保加利亚语等使用的文字 */
DEF(Cyrillic, "Cyrl")
/** @brief 塞浦路斯 - 米诺斯文字 (Cypro Minoan) - 古希腊文字 */
DEF(Cypro_Minoan, "Cpmn")
/** @brief 德撒律文 (Deseret) - 摩门教设计的英语拼音文字 */
DEF(Deseret, "Dsrt")
/** @brief 天城文 (Devanagari) - 印地语、梵语等使用的文字 */
DEF(Devanagari, "Deva")
/** @brief 迪维斯阿库鲁文 (Dives Akuru) - 马尔代夫古文字 */
DEF(Dives_Akuru, "Diak")
/** @brief 多格拉文 (Dogra) - 印度查谟地区文字 */
DEF(Dogra, "Dogr")
/** @brief 迪普洛扬文 (Duployan) - 速记符号系统 */
DEF(Duployan, "Dupl")
/** @brief 埃及象形文字 (Egyptian Hieroglyphs) - 古埃及文字 */
DEF(Egyptian_Hieroglyphs, "Egyp")
/** @brief 艾尔巴桑文 (Elbasan) - 阿尔巴尼亚古文字 */
DEF(Elbasan, "Elba")
/** @brief 埃利迈文 (Elymaic) - 古伊朗文字 */
DEF(Elymaic, "Elym")
/** @brief 埃塞俄比亚文 (Ethiopic) - 阿姆哈拉语、提格里尼亚语文字 */
DEF(Ethiopic, "Ethi")
/** @brief 加拉文 (Garay) - 塞内加尔加拉语文字 */
DEF(Garay, "Gara")
/** @brief 格鲁吉亚文 (Georgian) - 格鲁吉亚语文字 */
DEF(Georgian, "Geor")
/** @brief 格拉哥里文 (Glagolitic) - 古斯拉夫文字，最古老的斯拉夫字母 */
DEF(Glagolitic, "Glag")
/** @brief 哥特文 (Gothic) - 日耳曼哥特语文字 */
DEF(Gothic, "Goth")
/** @brief 格兰塔文 (Grantha) - 印度南部梵语文字 */
DEF(Grantha, "Gran")
/** @brief 希腊文 (Greek) - 希腊语文字 */
DEF(Greek, "Grek")
/** @brief 古吉拉特文 (Gujarati) - 印度古吉拉特语文字 */
DEF(Gujarati, "Gujr")
/** @brief 贡迪文 (Gunjala Gondi) - 印度贡迪语文字 */
DEF(Gunjala_Gondi, "Gong")
/** @brief 古木基文 (Gurmukhi) - 旁遮普语文字 */
DEF(Gurmukhi, "Guru")
/** @brief 古隆克玛文 (Gurung Khema) - 尼泊尔古隆语文字 */
DEF(Gurung_Khema, "Gukh")
/** @brief 汉字 (Han) - 中文、日文汉字、韩文汉字 */
DEF(Han, "Hani")
/** @brief 谚文 (Hangul) - 韩语字母 */
DEF(Hangul, "Hang")
/** @brief 哈尼菲罗兴亚文 (Hanifi Rohingya) - 罗兴亚语文字 */
DEF(Hanifi_Rohingya, "Rohg")
/** @brief 哈努诺文 (Hanunoo) - 菲律宾民都洛岛文字 */
DEF(Hanunoo, "Hano")
/** @brief 哈特兰文 (Hatran) - 古美索不达米亚文字 */
DEF(Hatran, "Hatr")
/** @brief 希伯来文 (Hebrew) - 希伯来语、意第绪语文字 */
DEF(Hebrew, "Hebr")
/** @brief 平假名 (Hiragana) - 日文音节文字 */
DEF(Hiragana, "Hira")
/** @brief 帝国阿拉米文 (Imperial Aramaic) - 古波斯帝国官方文字 */
DEF(Imperial_Aramaic, "Armi")
/** @brief 继承字符 (Inherited) - 从上下文继承脚本的字符（组合标记等） */
DEF(Inherited, "Zinh,Qaai")
/** @brief 碑铭巴列维文 (Inscriptional Pahlavi) - 古波斯碑铭文字 */
DEF(Inscriptional_Pahlavi, "Phli")
/** @brief 碑铭安息文 (Inscriptional Parthian) - 古安息帝国文字 */
DEF(Inscriptional_Parthian, "Prti")
/** @brief 爪哇文 (Javanese) - 印度尼西亚爪哇语文字 */
DEF(Javanese, "Java")
/** @brief 凯提文 (Kaithi) - 印度北部古文字 */
DEF(Kaithi, "Kthi")
/** @brief 卡纳达文 (Kannada) - 印度卡纳达语文字 */
DEF(Kannada, "Knda")
/** @brief 片假名 (Katakana) - 日文音节文字，用于外来语 */
DEF(Katakana, "Kana")
/** @brief 片假名或平假名 (Katakana Or Hiragana) - 日文混合音节文字 */
DEF(Katakana_Or_Hiragana, "Hrkt")
/** @brief 卡威文 (Kawi) - 印度尼西亚古爪哇语文字 */
DEF(Kawi, "Kawi")
/** @brief 克耶李文 (Kayah Li) - 缅甸克耶族文字 */
DEF(Kayah_Li, "Kali")
/** @brief 佉卢文 (Kharoshthi) - 古印度西北部文字 */
DEF(Kharoshthi, "Khar")
/** @brief 高棉文 (Khmer) - 柬埔寨高棉语文字 */
DEF(Khmer, "Khmr")
/** @brief 霍加基文 (Khojki) - 印度伊斯兰教社区文字 */
DEF(Khojki, "Khoj")
/** @brief 契丹小字 (Khitan Small Script) - 辽代契丹族文字 */
DEF(Khitan_Small_Script, "Kits")
/** @brief 库达瓦迪文 (Khudawadi) - 印度信德语文字 */
DEF(Khudawadi, "Sind")
/** @brief 基拉特莱文 (Kirat Rai) - 尼泊尔基拉特语文字 */
DEF(Kirat_Rai, "Krai")
/** @brief 老挝文 (Lao) - 老挝语文字 */
DEF(Lao, "Laoo")
/** @brief 拉丁文 (Latin) - 英语、法语、德语等使用的文字 */
DEF(Latin, "Latn")
/** @brief 雷布查文 (Lepcha) - 锡金雷布查语文字 */
DEF(Lepcha, "Lepc")
/** @brief 林布文 (Limbu) - 尼泊尔林布语文字 */
DEF(Limbu, "Limb")
/** @brief 线形文字 A (Linear A) - 古希腊米诺斯文字，尚未破译 */
DEF(Linear_A, "Lina")
/** @brief 线形文字 B (Linear B) - 古希腊迈锡尼文字，已破译 */
DEF(Linear_B, "Linb")
/** @brief 傈僳文 (Lisu) - 中国傈僳族文字 */
DEF(Lisu, "Lisu")
/** @brief 吕西亚文 (Lycian) - 古安纳托利亚文字 */
DEF(Lycian, "Lyci")
/** @brief 吕底亚文 (Lydian) - 古安纳托利亚文字 */
DEF(Lydian, "Lydi")
/** @brief 望加锡文 (Makasar) - 印度尼西亚苏拉威西文字 */
DEF(Makasar, "Maka")
/** @brief 马哈贾尼文 (Mahajani) - 印度商业记账文字 */
DEF(Mahajani, "Mahj")
/** @brief 马拉雅拉姆文 (Malayalam) - 印度马拉雅拉姆语文字 */
DEF(Malayalam, "Mlym")
/** @brief 曼达文 (Mandaic) - 伊拉克曼达语文字 */
DEF(Mandaic, "Mand")
/** @brief 摩尼文 (Manichaean) - 摩尼教经文文字 */
DEF(Manichaean, "Mani")
/** @brief 马尔琴文 (Marchen) - 中国藏族马尔琴方言文字 */
DEF(Marchen, "Marc")
/** @brief 马萨拉姆贡迪文 (Masaram Gondi) - 印度贡迪语文字 */
DEF(Masaram_Gondi, "Gonm")
/** @brief 梅德法伊德林文 (Medefaidrin) - 尼日利亚人造文字 */
DEF(Medefaidrin, "Medf")
/** @brief 梅泰文 (Meetei Mayek) - 印度曼尼普尔邦文字 */
DEF(Meetei_Mayek, "Mtei")
/** @brief 门德文 (Mende Kikakui) - 塞拉利昂门德语文字 */
DEF(Mende_Kikakui, "Mend")
/** @brief 麦罗埃草书 (Meroitic Cursive) - 古努比亚文字 */
DEF(Meroitic_Cursive, "Merc")
/** @brief 麦罗埃象形文字 (Meroitic Hieroglyphs) - 古努比亚文字 */
DEF(Meroitic_Hieroglyphs, "Mero")
/** @brief 苗文 (Miao) - 中国苗族文字（柏格理苗文） */
DEF(Miao, "Plrd")
/** @brief 莫迪文 (Modi) - 印度马拉地语古文字 */
DEF(Modi, "Modi")
/** @brief 蒙古文 (Mongolian) - 蒙古语传统文字 */
DEF(Mongolian, "Mong")
/** @brief 姆罗文 (Mro) - 缅甸姆罗族文字 */
DEF(Mro, "Mroo")
/** @brief 木尔坦文 (Multani) - 印度旁遮普语文字 */
DEF(Multani, "Mult")
/** @brief 缅甸文 (Myanmar) - 缅甸语文字 */
DEF(Myanmar, "Mymr")
/** @brief 纳巴泰文 (Nabataean) - 古阿拉伯文字，阿拉伯字母的祖先 */
DEF(Nabataean, "Nbat")
/** @brief 纳格蒙达里文 (Nag Mundari) - 印度蒙达里语文字 */
DEF(Nag_Mundari, "Nagm")
/** @brief 南迪纳加里文 (Nandinagari) - 印度梵语文字 */
DEF(Nandinagari, "Nand")
/** @brief 新傣仂文 (New Tai Lue) - 中国傣族文字 */
DEF(New_Tai_Lue, "Talu")
/** @brief 尼瓦尔文 (Newa) - 尼泊尔尼瓦尔语文字 */
DEF(Newa, "Newa")
/** @brief 西非书面文字 (Nko) - 西非曼德语族文字 */
DEF(Nko, "Nkoo")
/** @brief 女书 (Nushu) - 中国湖南女性专用文字 */
DEF(Nushu, "Nshu")
/** @brief 苗文 (Nyiakeng Puachue Hmong) - 中国苗族文字 */
DEF(Nyiakeng_Puachue_Hmong, "Hmnp")
/** @brief 欧甘文 (Ogham) - 古爱尔兰文字 */
DEF(Ogham, "Ogam")
/** @brief 桑塔利文 (Ol Chiki) - 印度桑塔利语文字 */
DEF(Ol_Chiki, "Olck")
/** @brief 奥尔奥纳尔文 (Ol Onal) - 印度蒙达语族文字 */
DEF(Ol_Onal, "Onao")
/** @brief 古匈牙利文 (Old Hungarian) - 匈牙利族古文字 */
DEF(Old_Hungarian, "Hung")
/** @brief 古意大利文 (Old Italic) - 古意大利半岛文字，拉丁字母的祖先 */
DEF(Old_Italic, "Ital")
/** @brief 古北阿拉伯文 (Old North Arabian) - 古阿拉伯文字 */
DEF(Old_North_Arabian, "Narb")
/** @brief 古彼尔姆文 (Old Permic) - 古俄罗斯科米语文字 */
DEF(Old_Permic, "Perm")
/** @brief 古波斯文 (Old Persian) - 阿契美尼德王朝文字 */
DEF(Old_Persian, "Xpeo")
/** @brief 古粟特文 (Old Sogdian) - 古中亚粟特语文字 */
DEF(Old_Sogdian, "Sogo")
/** @brief 古南阿拉伯文 (Old South Arabian) - 古也门文字 */
DEF(Old_South_Arabian, "Sarb")
/** @brief 古突厥文 (Old Turkic) - 突厥族古文字（如尼文） */
DEF(Old_Turkic, "Orkh")
/** @brief 古维吾尔文 (Old Uyghur) - 维吾尔族古文字 */
DEF(Old_Uyghur, "Ougr")
/** @brief 奥里亚文 (Oriya) - 印度奥里亚语文字 */
DEF(Oriya, "Orya")
/** @brief 奥塞奇文 (Osage) - 北美奥塞奇族文字 */
DEF(Osage, "Osge")
/** @brief 奥斯曼亚文 (Osmanya) - 索马里语文字 */
DEF(Osmanya, "Osma")
/** @brief 巴哈文 (Pahawh Hmong) - 老挝苗族文字 */
DEF(Pahawh_Hmong, "Hmng")
/** @brief 巴尔米拉文 (Palmyrene) - 古叙利亚巴尔米拉文字 */
DEF(Palmyrene, "Palm")
/** @brief 保钦豪文 (Pau Cin Hau) - 缅甸钦族文字 */
DEF(Pau_Cin_Hau, "Pauc")
/** @brief 八思巴文 (Phags Pa) - 元朝官方文字，藏文衍生 */
DEF(Phags_Pa, "Phag")
/** @brief 腓尼基文 (Phoenician) - 腓尼基语文字，希腊字母的祖先 */
DEF(Phoenician, "Phnx")
/** @brief 圣咏巴列维文 (Psalter Pahlavi) - 古波斯宗教文字 */
DEF(Psalter_Pahlavi, "Phlp")
/** @brief 贾文 (Rejang) - 印度尼西亚苏门答腊文字 */
DEF(Rejang, "Rjng")
/** @brief 卢恩文 (Runic) - 日耳曼族古文字 */
DEF(Runic, "Runr")
/** @brief 撒马利亚文 (Samaritan) - 撒马利亚语文字 */
DEF(Samaritan, "Samr")
/** @brief 索拉什特拉文 (Saurashtra) - 印度索拉什特拉语文字 */
DEF(Saurashtra, "Saur")
/** @brief 夏拉达文 (Sharada) - 印度克什米尔古文字 */
DEF(Sharada, "Shrd")
/** @brief 萧伯纳文 (Shavian) - 英语拼音文字，为萧伯纳设计 */
DEF(Shavian, "Shaw")
/** @brief 悉昙文 (Siddham) - 梵语佛教经文文字 */
DEF(Siddham, "Sidd")
/** @brief 西德提文 (Sidetic) - 古安纳托利亚文字 */
DEF(Sidetic, "Sidt")
/** @brief 手势书写 (SignWriting) - 手语书写系统 */
DEF(SignWriting, "Sgnw")
/** @brief 僧伽罗文 (Sinhala) - 斯里兰卡僧伽罗语文字 */
DEF(Sinhala, "Sinh")
/** @brief 粟特文 (Sogdian) - 古中亚粟特语文字 */
DEF(Sogdian, "Sogd")
/** @brief 索拉文 (Sora Sompeng) - 印度索拉族文字 */
DEF(Sora_Sompeng, "Sora")
/** @brief 索永布文 (Soyombo) - 蒙古佛教符号文字 */
DEF(Soyombo, "Soyo")
/** @brief 巽他文 (Sundanese) - 印度尼西亚巽他语文字 */
DEF(Sundanese, "Sund")
/** @brief 苏努瓦尔文 (Sunuwar) - 尼泊尔苏努瓦尔语文字 */
DEF(Sunuwar, "Sunu")
/** @brief 锡洛蒂那格里文 (Syloti Nagri) - 孟加拉国锡尔赫特语文字 */
DEF(Syloti_Nagri, "Sylo")
/** @brief 叙利亚文 (Syriac) - 叙利亚语、阿拉米语文字 */
DEF(Syriac, "Syrc")
/** @brief 他加禄文 (Tagalog) - 菲律宾吕宋岛文字 */
DEF(Tagalog, "Tglg")
/** @brief 塔格班瓦文 (Tagbanwa) - 菲律宾巴拉望岛文字 */
DEF(Tagbanwa, "Tagb")
/** @brief 傣仂文 (Tai Le) - 中国傣族文字 */
DEF(Tai_Le, "Tale")
/** @brief 兰纳文 (Tai Tham) - 泰国北部兰纳文字 */
DEF(Tai_Tham, "Lana")
/** @brief 傣担文 (Tai Viet) - 越南傣族文字 */
DEF(Tai_Viet, "Tavt")
/** @brief 傣幽文 (Tai Yo) - 老挝傣族文字 */
DEF(Tai_Yo, "Tayo")
/** @brief 塔克里文 (Takri) - 印度喜马偕尔邦文字 */
DEF(Takri, "Takr")
/** @brief 泰米尔文 (Tamil) - 印度泰米尔语文字 */
DEF(Tamil, "Taml")
/** @brief 西夏文 (Tangut) - 西夏王朝文字 */
DEF(Tangut, "Tang")
/** @brief 泰卢固文 (Telugu) - 印度泰卢固语文字 */
DEF(Telugu, "Telu")
/** @brief 塔那文 (Thaana) - 马尔代夫迪维希语文字 */
DEF(Thaana, "Thaa")
/** @brief 泰文 (Thai) - 泰语文字 */
DEF(Thai, "Thai")
/** @brief 藏文 (Tibetan) - 藏语文字 */
DEF(Tibetan, "Tibt")
/** @brief 提非纳文 (Tifinagh) - 北非柏柏尔语文字 */
DEF(Tifinagh, "Tfng")
/** @brief 提尔胡塔文 (Tirhuta) - 尼泊尔迈蒂利语文字 */
DEF(Tirhuta, "Tirh")
/** @brief 唐萨文 (Tangsa) - 缅甸唐萨族文字 */
DEF(Tangsa, "Tnsa")
/** @brief 托德里文 (Todhri) - 阿尔巴尼亚古文字 */
DEF(Todhri, "Todr")
/** @brief 托隆西基文 (Tolong Siki) - 印度阿萨姆文字 */
DEF(Tolong_Siki, "Tols")
/** @brief 托托文 (Toto) - 不丹托托族文字 */
DEF(Toto, "Toto")
/** @brief 图卢 - 提加拉文 (Tulu Tigalari) - 印度卡纳塔克邦文字 */
DEF(Tulu_Tigalari, "Tutg")
/** @brief 乌加里特文 (Ugaritic) - 古叙利亚楔形字母文字 */
DEF(Ugaritic, "Ugar")
/** @brief 瓦伊文 (Vai) - 利比里亚瓦伊语文字 */
DEF(Vai, "Vaii")
/** @brief 维特库奇文 (Vithkuqi) - 阿尔巴尼亚古文字 */
DEF(Vithkuqi, "Vith")
/** @brief 万乔文 (Wancho) - 印度那加兰邦文字 */
DEF(Wancho, "Wcho")
/** @brief 瓦朗 - 奇蒂文 (Warang Citi) - 印度霍语文字 */
DEF(Warang_Citi, "Wara")
/** @brief 雅兹迪文 (Yezidi) - 伊拉克雅兹迪族文字 */
DEF(Yezidi, "Yezi")
/** @brief 彝文 (Yi) - 中国彝族文字 */
DEF(Yi, "Yiii")
/** @brief 扎纳巴扎尔方文 (Zanabazar Square) - 蒙古佛教文字 */
DEF(Zanabazar_Square, "Zanb")
/**
 * @}
 */

#endif /* UNICODE_SCRIPT */

#ifdef UNICODE_PROP_LIST
/**
 * @defgroup Unicode_Properties Unicode 二元属性
 * 
 * Unicode 二元属性是字符的布尔特征，用于描述字符的各种特性。
 * 属性分为三类：
 * 
 * 1. 内部属性（不导出到正则表达式）：用于内部优化和特殊处理
 * 2. 导出到 JavaScript 的属性：可在 JS 代码中通过 \p{...} 访问
 * 3. 其他二元属性：标准 Unicode 属性
 * 
 * 属性用途：
 * - 正则表达式匹配：\p{ASCII}, \p{Emoji}, \p{Uppercase}
 * - 字符分类和处理
 * - 文本规范化
 * 
 * @{
 */

/**
 * @name 内部属性（不导出到正则表达式）
 * @brief 这些属性仅用于内部处理，不向用户暴露
 * @{
 */

/** @brief 连字符属性 */
DEF(Hyphen, "")
/** @brief 其他数学字符 */
DEF(Other_Math, "")
/** @brief 其他字母字符 */
DEF(Other_Alphabetic, "")
/** @brief 其他小写字母 */
DEF(Other_Lowercase, "")
/** @brief 其他大写字母 */
DEF(Other_Uppercase, "")
/** @brief 其他图素扩展符 */
DEF(Other_Grapheme_Extend, "")
/** @brief 其他默认可忽略码点 */
DEF(Other_Default_Ignorable_Code_Point, "")
/** @brief 其他 ID_Start 字符 */
DEF(Other_ID_Start, "")
/** @brief 其他 ID_Continue 字符 */
DEF(Other_ID_Continue, "")
/** @brief 前缀连接标记 */
DEF(Prepended_Concatenation_Mark, "")
/**
 * @}
 */

/**
 * @name 计算的派生属性（用于缩小表格）
 * @brief 这些属性通过计算得出，用于优化存储空间
 * @{
 */

/** @brief ID_Continue 派生属性（内部版本） */
DEF(ID_Continue1, "")
/** @brief XID_Start 派生属性（内部版本） */
DEF(XID_Start1, "")
/** @brief XID_Continue 派生属性（内部版本） */
DEF(XID_Continue1, "")
/** @brief 标题大写时变化的字符（内部版本） */
DEF(Changes_When_Titlecased1, "")
/** @brief 大小写折叠时变化的字符（内部版本） */
DEF(Changes_When_Casefolded1, "")
/** @brief NFKC 大小写折叠时变化的字符（内部版本） */
DEF(Changes_When_NFKC_Casefolded1, "")
/** @brief 基本 Emoji（内部版本 1） */
DEF(Basic_Emoji1, "")
/** @brief 基本 Emoji（内部版本 2） */
DEF(Basic_Emoji2, "")
/** @brief RGI Emoji 修饰符序列 */
DEF(RGI_Emoji_Modifier_Sequence, "")
/** @brief RGI Emoji 旗帜序列 */
DEF(RGI_Emoji_Flag_Sequence, "") 
/** @brief Emoji 键帽序列 */
DEF(Emoji_Keycap_Sequence, "")
/**
 * @}
 */

/**
 * @name 导出到 JavaScript 的属性
 * @brief 这些属性可在 JS 正则表达式中使用
 * @{
 */

/** @brief ASCII 十六进制数字 (0-9, A-F, a-f) */
DEF(ASCII_Hex_Digit, "AHex")
/** @brief 双向控制字符 */
DEF(Bidi_Control, "Bidi_C")
/** @brief 破折号字符 */
DEF(Dash, "")
/** @brief 已弃用字符 */
DEF(Deprecated, "Dep")
/** @brief 变音符号 */
DEF(Diacritic, "Dia")
/** @brief 扩展符（用于构成复合字符） */
DEF(Extender, "Ext")
/** @brief 十六进制数字 */
DEF(Hex_Digit, "Hex")
/** @brief IDEO 表意文字描述符一元运算符 */
DEF(IDS_Unary_Operator, "IDSU")
/** @brief IDEO 表意文字描述符二元运算符 */
DEF(IDS_Binary_Operator, "IDSB")
/** @brief IDEO 表意文字描述符三元运算符 */
DEF(IDS_Trinary_Operator, "IDST")
/** @brief 表意文字（中日韩统一表意文字） */
DEF(Ideographic, "Ideo")
/** @brief 连接控制字符（零宽连接符等） */
DEF(Join_Control, "Join_C")
/** @brief 逻辑顺序异常字符 */
DEF(Logical_Order_Exception, "LOE")
/** @brief 修饰组合标记 */
DEF(Modifier_Combining_Mark, "MCM")
/** @brief 非字符码点（Unicode 保留，永不分配） */
DEF(Noncharacter_Code_Point, "NChar")
/** @brief 模式语法字符（正则表达式特殊字符） */
DEF(Pattern_Syntax, "Pat_Syn")
/** @brief 模式空白字符 */
DEF(Pattern_White_Space, "Pat_WS")
/** @brief 引号标记 */
DEF(Quotation_Mark, "QMark")
/** @brief 部首（汉字字典检索用） */
DEF(Radical, "")
/** @brief 区域指示符（用于 Emoji 旗帜） */
DEF(Regional_Indicator, "RI")
/** @brief 句子终止符 */
DEF(Sentence_Terminal, "STerm")
/** @brief 软点状字符（如 i, j 等带上有点的字母） */
DEF(Soft_Dotted, "SD")
/** @brief 终止标点 */
DEF(Terminal_Punctuation, "Term")
/** @brief 统一表意文字 */
DEF(Unified_Ideograph, "UIdeo")
/** @brief 变体选择符（用于选择字符的显示变体） */
DEF(Variation_Selector, "VS")
/** @brief 空白字符 */
DEF(White_Space, "space")
/** @brief 双向镜像字符（如括号） */
DEF(Bidi_Mirrored, "Bidi_M")
/** @brief Emoji 字符 */
DEF(Emoji, "")
/** @brief Emoji 组件（用于构成 Emoji 序列） */
DEF(Emoji_Component, "EComp")
/** @brief Emoji 修饰符（肤色修饰符） */
DEF(Emoji_Modifier, "EMod")
/** @brief Emoji 修饰符基础字符 */
DEF(Emoji_Modifier_Base, "EBase")
/** @brief 带 Emoji 呈现的字符（默认显示为彩色 Emoji） */
DEF(Emoji_Presentation, "EPres")
/** @brief 扩展象形图 */
DEF(Extended_Pictographic, "ExtPict")
/** @brief 默认可忽略码点 */
DEF(Default_Ignorable_Code_Point, "DI")
/** @brief 标识符起始字符（可用于变量名开头） */
DEF(ID_Start, "IDS")
/** @brief 大小写可忽略字符 */
DEF(Case_Ignorable, "CI")
/**
 * @}
 */

/**
 * @name 其他二元属性
 * @brief 标准 Unicode 二元属性
 * @{
 */

/** @brief ASCII 字符 (U+0000..U+007F) */
DEF(ASCII,"")
/** @brief 字母字符（可用于构成单词） */
DEF(Alphabetic, "Alpha")
/** @brief 任意字符（恒为 true） */
DEF(Any, "")
/** @brief 已分配字符（非 Cn 未分配） */
DEF(Assigned,"")
/** @brief 有大小写的字符 */
DEF(Cased, "")
/** @brief 大小写折叠时变化的字符 */
DEF(Changes_When_Casefolded, "CWCF")
/** @brief 大小写映射时变化的字符 */
DEF(Changes_When_Casemapped, "CWCM")
/** @brief 转小写时变化的字符 */
DEF(Changes_When_Lowercased, "CWL")
/** @brief NFKC 大小写折叠时变化的字符 */
DEF(Changes_When_NFKC_Casefolded, "CWKCF")
/** @brief 转标题大写时变化的字符 */
DEF(Changes_When_Titlecased, "CWT")
/** @brief 转大写时变化的字符 */
DEF(Changes_When_Uppercased, "CWU")
/** @brief 图素基础字符（可独立显示） */
DEF(Grapheme_Base, "Gr_Base")
/** @brief 图素扩展符（附加到基础字符） */
DEF(Grapheme_Extend, "Gr_Ext")
/** @brief 标识符延续字符（可用于变量名） */
DEF(ID_Continue, "IDC")
/** @brief ID 兼容数学起始字符 */
DEF(ID_Compat_Math_Start, "")
/** @brief ID 兼容数学延续字符 */
DEF(ID_Compat_Math_Continue, "")
/** @brief 变化时阻断字符（Changes When Casefolded 的缩写） */
DEF(InCB, "")
/** @brief 小写字母 */
DEF(Lowercase, "Lower")
/** @brief 数学字符（用于数学表达式） */
DEF(Math, "")
/** @brief 大写字母 */
DEF(Uppercase, "Upper")
/** @brief 扩展标识符延续字符（比 IDC 更严格） */
DEF(XID_Continue, "XIDC")
/** @brief 扩展标识符起始字符（比 IDS 更严格） */
DEF(XID_Start, "XIDS")
/**
 * @}
 */

/**
 * @name 内部索引表格
 * @brief 带索引的内部属性表格，用于优化查找
 * @{
 */

/** @brief 有大小写字符的内部索引表 */
DEF(Cased1, "")
/**
 * @}
 */

/**
 * @}
 */

#endif /* UNICODE_PROP_LIST */

#ifdef UNICODE_SEQUENCE_PROP_LIST
/**
 * @defgroup Unicode_Sequence_Properties Unicode 序列属性
 * 
 * Unicode 序列属性定义由多个码点组成的字符序列。
 * 这些序列在视觉上表现为单个字符，但底层由多个码点构成。
 * 
 * 序列类型：
 * - Basic Emoji: 基本 Emoji 字符
 * - Emoji Keycap Sequence: 键帽 Emoji（如 1️⃣, 2️⃣）
 * - RGI Emoji Modifier Sequence: 带肤色修饰符的 Emoji
 * - RGI Emoji Flag Sequence: 旗帜 Emoji（由区域指示符组成）
 * - RGI Emoji Tag Sequence: 标签序列 Emoji
 * - RGI Emoji ZWJ Sequence: ZWJ 连接序列 Emoji（如 👨‍👩‍👧‍👦）
 * - RGI Emoji: 所有推荐统一 Emoji（Recommended for General Interchange）
 * 
 * RGI (Recommended for General Interchange): Unicode 联盟推荐用于通用交换的 Emoji 集合
 * 
 * @{
 */

/** @brief 基本 Emoji 序列 */
DEF(Basic_Emoji)
/** @brief Emoji 键帽序列（数字 + 组合符 + 键帽） */
DEF(Emoji_Keycap_Sequence)
/** @brief RGI Emoji 修饰符序列（Emoji + 肤色修饰符） */
DEF(RGI_Emoji_Modifier_Sequence)
/** @brief RGI Emoji 旗帜序列（两个区域指示符组成国旗） */
DEF(RGI_Emoji_Flag_Sequence)
/** @brief RGI Emoji 标签序列（用于子地区旗帜等） */
DEF(RGI_Emoji_Tag_Sequence)
/** @brief RGI Emoji ZWJ 序列（零宽连接符连接多个 Emoji） */
DEF(RGI_Emoji_ZWJ_Sequence)
/** @brief 所有 RGI 推荐统一 Emoji */
DEF(RGI_Emoji)
/**
 * @}
 */

#endif /* UNICODE_SEQUENCE_PROP_LIST */
