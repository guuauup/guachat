/**
 * @file lv_ime_pinyin.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_ime_pinyin.h"
#if LV_USE_IME_PINYIN != 0

#include <stdio.h>
#include <string.h>

/*********************
 *      DEFINES
 *********************/
#define MY_CLASS    &lv_ime_pinyin_class

/* 候选栏横向滚动(无翻页箭头): 每个候选按钮的宽度单位 = 汉字数 + 1
   (1字→2单位, 2字→3单位…), 单位像素宽 LV_IME_PINYIN_CAND_UNIT_PX.
   这样 4~5 字整词也能完整显示, 候选总宽超过容器时左右拖动查看. */
#define LV_IME_PINYIN_CAND_UNIT_PX  20

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_ime_pinyin_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void lv_ime_pinyin_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void lv_ime_pinyin_style_change_event(lv_event_t * e);
static void lv_ime_pinyin_kb_event(lv_event_t * e);
static void lv_ime_pinyin_cand_panel_event(lv_event_t * e);

static void init_pinyin_dict(lv_obj_t * obj, lv_pinyin_dict_t * dict);
static void init_phrase_dict(lv_obj_t * obj, const lv_phrase_dict_t * dict);
static void pinyin_input_proc(lv_obj_t * obj);
static char * pinyin_search_matching(lv_obj_t * obj, char * py_str, uint16_t * cand_num);
static char * pinyin_longest_syllable_prefix(lv_obj_t * obj, char * py_str, uint16_t * cand_num, uint8_t * syl_len);
static bool pinyin_is_multisyllable(lv_obj_t * obj, char * py_str);
static bool pinyin_phrase_has_prefix(lv_obj_t * obj, char * py_str);
static bool pinyin_segmented_match(lv_obj_t * obj, const char * py, const char * word, const char * input);
static bool pinyin_segmented_has_prefix(lv_obj_t * obj, const char * input);
static void pinyin_build_cand_map(lv_obj_t * obj);
static void pinyin_ime_clear_data(lv_obj_t * obj);

#if LV_IME_PINYIN_USE_K9_MODE
    static void pinyin_k9_init_data(lv_obj_t * obj);
    static void pinyin_k9_get_legal_py(lv_obj_t * obj, char * k9_input, const char * py9_map[]);
    static bool pinyin_k9_is_valid_py(lv_obj_t * obj, char * py_str);
    static void pinyin_k9_fill_cand(lv_obj_t * obj);
    static void pinyin_k9_cand_page_proc(lv_obj_t * obj, uint16_t dir);
#endif

/**********************
 *  STATIC VARIABLES
 **********************/
const lv_obj_class_t lv_ime_pinyin_class = {
    .constructor_cb = lv_ime_pinyin_constructor,
    .destructor_cb  = lv_ime_pinyin_destructor,
    .width_def      = LV_SIZE_CONTENT,
    .height_def     = LV_SIZE_CONTENT,
    .group_def      = LV_OBJ_CLASS_GROUP_DEF_TRUE,
    .instance_size  = sizeof(lv_ime_pinyin_t),
    .base_class     = &lv_obj_class
};

#if LV_IME_PINYIN_USE_K9_MODE
static char * lv_btnm_def_pinyin_k9_map[LV_IME_PINYIN_K9_CAND_TEXT_NUM + 21] = {\
                                                                                ",\0", "1#\0",  "abc \0", "def\0",  LV_SYMBOL_BACKSPACE"\0", "\n\0",
                                                                                ".\0", "ghi\0", "jkl\0", "mno\0",  LV_SYMBOL_KEYBOARD"\0", "\n\0",
                                                                                "?\0", "pqrs\0", "tuv\0", "wxyz\0",  LV_SYMBOL_NEW_LINE"\0", "\n\0",
                                                                                LV_SYMBOL_LEFT"\0", "\0"
                                                                               };

static lv_btnmatrix_ctrl_t default_kb_ctrl_k9_map[LV_IME_PINYIN_K9_CAND_TEXT_NUM + 17] = { 1 };
static char   lv_pinyin_k9_cand_str[LV_IME_PINYIN_K9_CAND_TEXT_NUM + 2][LV_IME_PINYIN_K9_MAX_INPUT] = {0};
#endif

#if LV_IME_PINYIN_USE_DEFAULT_DICT
lv_pinyin_dict_t lv_ime_pinyin_def_dict[] = {
    { "a", "啊" },
    { "ai", "愛" },
    { "an", "安暗案" },
    { "ba", "吧把爸八" },
    { "bai", "百白敗" },
    { "ban", "半般辦" },
    { "bang", "旁" },
    { "bao", "保薄包報" },
    { "bei", "被背悲北杯備" },
    { "ben", "本" },
    { "bi", "必比避鼻彼筆秘閉" },
    { "bian", "便邊變変辺" },
    { "biao", "表標" },
    { "bie", "別" },
    { "bing", "病並氷" },
    { "bo", "波薄泊" },
    { "bu", "不布步部捕補歩" },
    { "ca", "察" },
    { "cai", "才材菜財採" },
    { "can", "参残參" },
    { "ce", "策側" },
    { "ceng", "曾" },
    { "cha", "差查茶" },
    { "chai", "差" },
    { "chan", "產産單" },
    { "chang", "場廠" },
    { "chao", "超朝" },
    { "che", "車" },
    { "cheng", "成程乗" },
    { "chi", "尺吃持赤池遅歯" },
    { "chong", "充种重種" },
    { "chu", "出初楚触處処" },
    { "chuan", "川船傳" },
    { "chuang", "創窓" },
    { "chun", "春" },
    { "ci", "此次辞差" },
    { "cong", "從従" },
    { "cu", "卒" },
    { "cun", "存村" },
    { "cuo", "錯" },
    { "da", "大打答達" },
    { "dai", "代待帯帶貸" },
    { "dan", "但担擔誕單単" },
    { "dang", "当党當黨" },
    { "dao", "到道盗導島辺" },
    { "de", "的得" },
    { "dei", "" },
    { "deng", "等" },
    { "di", "地得低底弟第締" },
    { "dian", "点电店點電" },
    { "diao", "調" },
    { "ding", "定町" },
    { "dong", "冬東動働凍" },
    { "du", "独度都渡読" },
    { "duan", "段断短斷" },
    { "dui", "對対" },
    { "duo", "多駄" },
    { "e", "嗯悪" },
    { "en", "嗯" },
    { "er", "而耳二兒" },
    { "fa", "乏法發発髪" },
    { "fan", "反返犯番仮販飯範払" },
    { "fang", "方放房坊訪" },
    { "fei", "非飛費" },
    { "fen", "分份" },
    { "feng", "風豐" },
    { "fou", "否不" },
    { "fu", "父夫富服符付附府幅婦復複負払" },
    { "gai", "改概該" },
    { "gan", "甘感敢" },
    { "gang", "港剛" },
    { "gao", "告高" },
    { "ge", "各格歌革割個" },
    { "gei", "給" },
    { "gen", "跟根" },
    { "geng", "更" },
    { "gong", "工共供功公" },
    { "gou", "夠構溝" },
    { "gu", "古故鼓" },
    { "guai", "掛" },
    { "guan", "官管慣館觀関關" },
    { "guang", "光広" },
    { "gui", "規帰" },
    { "guo", "果国裏菓國過" },
    { "hai", "孩海害還" },
    { "han", "寒漢" },
    { "hang", "航行" },
    { "hao", "好号" },
    { "he", "合和喝何荷" },
    { "hei", "黒" },
    { "hen", "很" },
    { "heng", "行横" },
    { "hou", "厚喉候後" },
    { "hu", "乎呼湖護" },
    { "hua", "化画花話畫劃" },
    { "huai", "壊劃" },
    { "huan", "緩環歡還換" },
    { "huang", "黄" },
    { "hui", "回会慧絵揮會" },
    { "hun", "混婚" },
    { "huo", "活或火獲" },
    { "i", "" },
    { "ji", "己计及机既急季寄技即集基祭系奇紀積計記済幾際極繼績機濟" },
    { "jia", "家加價" },
    { "jian", "件建健肩見減間検簡漸" },
    { "jiang", "降強講將港" },
    { "jiao", "叫教交角覚覺較學" },
    { "jie", "介借接姐皆届界解結階節價" },
    { "jin", "今近禁金僅進" },
    { "jing", "京境景静精經経" },
    { "jiu", "就久九酒究" },
    { "ju", "句具局居決挙據舉" },
    { "jue", "角覚覺" },
    { "jun", "均" },
    { "kai", "開" },
    { "kan", "看刊" },
    { "kang", "康" },
    { "kao", "考" },
    { "ke", "可刻科克客渇課" },
    { "ken", "肯" },
    { "kong", "空控" },
    { "kou", "口" },
    { "ku", "苦庫" },
    { "kuai", "快塊会會" },
    { "kuang", "況" },
    { "kun", "困" },
    { "kuo", "括拡適" },
    { "la", "拉啦落" },
    { "lai", "来來頼" },
    { "lao", "老絡落" },
    { "le", "了楽樂" },
    { "lei", "類" },
    { "leng", "冷" },
    { "li", "力立利理例礼離麗裡勵歷" },
    { "lian", "連練臉聯" },
    { "liang", "良量涼兩両" },
    { "liao", "料" },
    { "lie", "列" },
    { "lin", "林隣賃" },
    { "ling", "另令領" },
    { "liu", "六留流" },
    { "lu", "律路録緑陸履慮" },
    { "lv", "旅" },
    { "lun", "輪論" },
    { "luo", "落絡" },
    { "ma", "媽嗎嘛" },
    { "mai", "買売" },
    { "man", "滿" },
    { "mang", "忙" },
    { "mao", "毛猫貿" },
    { "me", "麼" },
    { "mei", "美妹每沒毎媒" },
    { "men", "們" },
    { "mi", "米密秘" },
    { "mian", "免面勉眠" },
    { "miao", "描" },
    { "min", "民皿" },
    { "ming", "命明名" },
    { "mo", "末模麼" },
    { "mou", "某" },
    { "mu", "母木目模" },
    { "na", "那哪拿內南" },
    { "nan", "男南難" },
    { "nao", "腦" },
    { "ne", "那哪呢" },
    { "nei", "内那哪內" },
    { "neng", "能" },
    { "ni", "你妳呢" },
    { "nian", "年念" },
    { "niang", "娘" },
    { "nin", "您" },
    { "ning", "凝" },
    { "niu", "牛" },
    { "nong", "農濃" },
    { "nu", "女努" },
    { "nuan", "暖" },
    { "o", "" },
    { "ou", "歐" },
    { "pa", "怕" },
    { "pian", "片便" },
    { "pai", "迫派排" },
    { "pan", "判番" },
    { "pang", "旁" },
    { "pei", "配" },
    { "peng", "朋" },
    { "pi", "疲否" },
    { "pin", "品貧" },
    { "ping", "平評" },
    { "po", "迫破泊頗" },
    { "pu", "普僕" },
    { "qi", "起其奇七气期泣企妻契気" },
    { "qian", "嵌浅千前鉛錢針" },
    { "qiang", "強將" },
    { "qiao", "橋繰" },
    { "qie", "且切契" },
    { "qin", "寝勤親" },
    { "qing", "青清情晴輕頃請軽" },
    { "qiu", "求秋球" },
    { "qu", "去取趣曲區" },
    { "quan", "全犬券" },
    { "que", "缺確卻" },
    { "ran", "然" },
    { "rang", "讓" },
    { "re", "熱" },
    { "ren", "人任認" },
    { "reng", "仍" },
    { "ri", "日" },
    { "rong", "容" },
    { "rou", "弱若肉" },
    { "ru", "如入" },
    { "ruan", "軟" },
    { "sai", "賽" },
    { "san", "三" },
    { "sao", "騒繰" },
    { "se", "色" },
    { "sen", "森" },
    { "sha", "砂" },
    { "shan", "善山單" },
    { "shang", "上尚商" },
    { "shao", "少紹" },
    { "shaung", "雙" },
    { "she", "社射設捨渉" },
    { "shei", "誰" },
    { "shen", "什申深甚身伸沈神" },
    { "sheng", "生声昇勝乗聲" },
    { "shi", "是失示食时事式十石施使世实史室市始柿氏士仕拭時視師試適実實識" },
    { "shou", "手首守受授" },
    { "shu", "束数暑殊樹書屬輸術" },
    { "shui", "水説說誰" },
    { "shuo", "数説說" },
    { "si", "思寺司四私似死価" },
    { "song", "送" },
    { "su", "速宿素蘇訴" },
    { "suan", "算酸" },
    { "sui", "隨雖歲歳" },
    { "sun", "孫" },
    { "suo", "所" },
    { "ta", "她他它牠" },
    { "tai", "太台態臺" },
    { "tan", "探談曇" },
    { "tang", "糖" },
    { "tao", "桃逃套討" },
    { "te", "特" },
    { "ti", "体提替題體戻" },
    { "tian", "天田" },
    { "tiao", "条條調" },
    { "tie", "鉄" },
    { "ting", "停庭聽町" },
    { "tong", "同童通痛统統" },
    { "tou", "投透頭" },
    { "tu", "土徒茶図" },
    { "tuan", "團" },
    { "tui", "推退" },
    { "tuo", "脱駄" },
    { "u", "" },
    { "v", "" },
    { "wai", "外" },
    { "wan", "完万玩晩腕灣" },
    { "wang", "忘望亡往網" },
    { "wei", "危位未味委為謂維違圍" },
    { "wen", "文温問聞" },
    { "wo", "我" },
    { "wu", "午物五無屋亡鳥務汚" },
    { "xi", "夕息西洗喜系昔席希析嬉膝細習係" },
    { "xia", "下夏狭暇" },
    { "xian", "先限嫌洗現見線顯" },
    { "xiang", "向相香像想象降項詳響" },
    { "xiao", "小笑消效校削咲" },
    { "xie", "写携些解邪械協謝寫契" },
    { "xin", "心信新辛" },
    { "xing", "行形性幸型星興" },
    { "xiong", "兄胸" },
    { "xiu", "休秀修" },
    { "xu", "須需許續緒続" },
    { "xuan", "選懸" },
    { "xue", "学雪削靴學" },
    { "xun", "訓訊" },
    { "ya", "呀押壓" },
    { "yan", "言顔研煙嚴厳験驗塩" },
    { "yang", "央洋陽樣様" },
    { "yao", "要揺腰薬曜" },
    { "ye", "也野夜邪業葉" },
    { "yi", "一已亦依以移意医易伊役異億義議藝醫訳" },
    { "yin", "因引音飲銀" },
    { "ying", "英迎影映應營営" },
    { "yong", "永用泳擁" },
    { "you", "又有右友由尤油遊郵誘優" },
    { "yu", "予育余雨浴欲愈御宇域語於魚與込" },
    { "yuan", "元原源院員円園遠猿願" },
    { "yue", "月越約楽" },
    { "yun", "雲伝運" },
    { "za", "雑" },
    { "zai", "在再載災" },
    { "zang", "蔵" },
    { "zao", "早造" },
    { "ze", "則擇責" },
    { "zen", "怎" },
    { "zeng", "曾增増" },
    { "zha", "札" },
    { "zhai", "宅擇" },
    { "zhan", "站展戰戦" },
    { "zhang", "丈長障帳張" },
    { "zhao", "找着朝招" },
    { "zhe", "者這" },
    { "zhen", "真震針" },
    { "zheng", "正整争政爭" },
    { "zhi", "之只知支止制至治直指值置智値紙製質誌織隻識職執" },
    { "zhong", "中种終重種眾" },
    { "zhou", "周州昼宙洲週" },
    { "zhu", "助主住柱株祝逐注著諸屬術" },
    { "zhuan", "专專転" },
    { "zhuang", "状狀" },
    { "zhui", "追" },
    { "zhun", "準" },
    { "zhuo", "着" },
    { "zi", "子自字姉資" },
    { "zong", "總" },
    { "zuo", "左做昨坐座作" },
    { "zu", "足祖族卒組" },
    { "zui", "最酔" },
    { "zou", "走" },
    {NULL, NULL}
};
#endif


/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
lv_obj_t * lv_ime_pinyin_create(lv_obj_t * parent)
{
    LV_LOG_INFO("begin");
    lv_obj_t * obj = lv_obj_class_create_obj(MY_CLASS, parent);
    lv_obj_class_init_obj(obj);
    return obj;
}


/*=====================
 * Setter functions
 *====================*/

/**
 * Set the keyboard of Pinyin input method.
 * @param obj  pointer to a Pinyin input method object
 * @param dict pointer to a Pinyin input method keyboard
 */
void lv_ime_pinyin_set_keyboard(lv_obj_t * obj, lv_obj_t * kb)
{
    if(kb) {
        LV_ASSERT_OBJ(kb, &lv_keyboard_class);
    }

    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    pinyin_ime->kb = kb;
    pinyin_ime->ta = lv_keyboard_get_textarea(kb);
    lv_obj_add_event_cb(pinyin_ime->kb, lv_ime_pinyin_kb_event, LV_EVENT_ALL, obj);
    lv_obj_align_to(pinyin_ime->cand_panel, pinyin_ime->kb, LV_ALIGN_TOP_MID, 0, 56);//选字栏位置高度
    lv_obj_set_height(pinyin_ime->cand_panel, 34);//选字栏高
    //lv_obj_align_to(pinyin_ime->cand_panel, pinyin_ime->kb, LV_ALIGN_OUT_TOP_MID, 0, 0);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Use a custom btnmatrix as the keyboard, with a separate textarea.
 * @param obj   pointer to a Pinyin input method object
 * @param btnm  pointer to a btnmatrix object to use as keyboard
 * @param ta    pointer to the Text Area to input characters into
 */
void lv_ime_pinyin_set_kb_btnmatrix(lv_obj_t * obj, lv_obj_t * btnm, lv_obj_t * ta)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    pinyin_ime->kb = btnm;
    pinyin_ime->ta = ta;
    lv_obj_add_event_cb(pinyin_ime->kb, lv_ime_pinyin_kb_event, LV_EVENT_ALL, obj);
    lv_obj_align_to(pinyin_ime->cand_panel, pinyin_ime->kb, LV_ALIGN_TOP_MID, 0, 60);//选字栏位置高度
    lv_obj_set_height(pinyin_ime->cand_panel, 34);//选字栏高
    //lv_obj_align_to(pinyin_ime->cand_panel, pinyin_ime->kb, LV_ALIGN_OUT_TOP_MID, 0, 0);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Set the dictionary of Pinyin input method.
 * @param obj  pointer to a Pinyin input method object
 * @param dict pointer to a Pinyin input method dictionary
 */
void lv_ime_pinyin_set_dict(lv_obj_t * obj, lv_pinyin_dict_t * dict)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    init_pinyin_dict(obj, dict);
}

void lv_ime_pinyin_set_phrase_dict(lv_obj_t * obj, const lv_phrase_dict_t * phrase_dict)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    init_phrase_dict(obj, phrase_dict);
}

void lv_ime_pinyin_set_word_score_cb(lv_obj_t * obj, uint16_t (*cb)(const char * word))
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;
    pinyin_ime->word_score_cb = cb;
}

void lv_ime_pinyin_set_word_note_cb(lv_obj_t * obj, void (*cb)(const char * word))
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;
    pinyin_ime->word_note_cb = cb;
}

void lv_ime_pinyin_set_sentence_enable(lv_obj_t * obj, bool en)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;
    pinyin_ime->sentence_enable = en ? 1 : 0;
    if(!pinyin_ime->sentence_enable) {
        pinyin_ime->has_sentence = 0;
        pinyin_ime->sentence[0] = '\0';
        pinyin_ime->sentence_chars = 0;
    }
}

/**
 * Set mode, 26-key input(k26) or 9-key input(k9).
 * @param obj  pointer to a Pinyin input method object
 * @param mode   the mode from 'lv_keyboard_mode_t'
 */
void lv_ime_pinyin_set_mode(lv_obj_t * obj, lv_ime_pinyin_mode_t mode)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    LV_ASSERT_OBJ(pinyin_ime->kb, &lv_keyboard_class);

    pinyin_ime->mode = mode;

#if LV_IME_PINYIN_USE_K9_MODE
    if(pinyin_ime->mode == LV_IME_PINYIN_MODE_K9) {
        pinyin_k9_init_data(obj);
        lv_keyboard_set_map(pinyin_ime->kb, LV_KEYBOARD_MODE_USER_1, (const char *)lv_btnm_def_pinyin_k9_map,
                            (const)default_kb_ctrl_k9_map);
        lv_keyboard_set_mode(pinyin_ime->kb, LV_KEYBOARD_MODE_USER_1);
    }
#endif
}

/*=====================
 * Getter functions
 *====================*/

/**
 * Set the dictionary of Pinyin input method.
 * @param obj  pointer to a Pinyin IME object
 * @return     pointer to the Pinyin IME keyboard
 */
lv_obj_t * lv_ime_pinyin_get_kb(lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    return pinyin_ime->kb;
}

/**
 * Set the dictionary of Pinyin input method.
 * @param obj  pointer to a Pinyin input method object
 * @return     pointer to the Pinyin input method candidate panel
 */
lv_obj_t * lv_ime_pinyin_get_cand_panel(lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    return pinyin_ime->cand_panel;
}

/**
 * Set the dictionary of Pinyin input method.
 * @param obj  pointer to a Pinyin input method object
 * @return     pointer to the Pinyin input method dictionary
 */
lv_pinyin_dict_t * lv_ime_pinyin_get_dict(lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    return pinyin_ime->dict;
}

/*=====================
 * Other functions
 *====================*/

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void lv_ime_pinyin_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);
    LV_TRACE_OBJ_CREATE("begin");

    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    pinyin_ime->sel_map[0] = "";

    pinyin_ime->mode = LV_IME_PINYIN_MODE_K26;
    pinyin_ime->k26_pinyin = 0;
    pinyin_ime->ta_count = 0;
    pinyin_ime->cand_num = 0;
    pinyin_ime->ta = NULL;
    lv_memset_00(pinyin_ime->input_char, sizeof(pinyin_ime->input_char));
    lv_memset_00(pinyin_ime->py_num, sizeof(pinyin_ime->py_num));
    lv_memset_00(pinyin_ime->py_pos, sizeof(pinyin_ime->py_pos));
    pinyin_ime->phrase_dict = NULL;
    lv_memset_00(pinyin_ime->phrase_num, sizeof(pinyin_ime->phrase_num));
    lv_memset_00(pinyin_ime->phrase_pos, sizeof(pinyin_ime->phrase_pos));
    pinyin_ime->word_score_cb = NULL;
    pinyin_ime->word_note_cb = NULL;

    pinyin_ime->has_sentence = 0;
    pinyin_ime->sentence_enable = 0;
    pinyin_ime->manual_sep = 0;
    pinyin_ime->sentence_chars = 0;
    lv_memset_00(pinyin_ime->sentence, sizeof(pinyin_ime->sentence));

    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);

    lv_obj_set_size(obj, LV_PCT(100), LV_PCT(55));
    lv_obj_align(obj, LV_ALIGN_BOTTOM_MID, 0, 0);

#if LV_IME_PINYIN_USE_DEFAULT_DICT
    init_pinyin_dict(obj, lv_ime_pinyin_def_dict);
#endif

    /* Init pinyin_ime->cand_panel: 横向滚动容器, 内含加宽的候选 btnmatrix(无翻页箭头).
       候选总宽超过容器时左右拖动查看, 省下的箭头空间让给更多候选. */
    pinyin_ime->cand_panel = lv_obj_create(lv_obj_get_parent(obj));
    lv_obj_set_size(pinyin_ime->cand_panel, LV_PCT(100), LV_PCT(5));
    lv_obj_add_flag(pinyin_ime->cand_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_scroll_dir(pinyin_ime->cand_panel, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(pinyin_ime->cand_panel, LV_SCROLLBAR_MODE_OFF);

    /* 容器样式: 白底无边框 */
    lv_obj_set_style_bg_opa(pinyin_ime->cand_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(pinyin_ime->cand_panel, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_width(pinyin_ime->cand_panel, 0, 0);
    lv_obj_set_style_pad_all(pinyin_ime->cand_panel, 0, 0);
    lv_obj_set_style_radius(pinyin_ime->cand_panel, 0, 0);
    lv_obj_set_style_base_dir(pinyin_ime->cand_panel, LV_BASE_DIR_LTR, 0);

    /* 候选汉字 btnmatrix: 宽度按候选内容动态设定(见 pinyin_build_cand_map), 超出容器左右滚动 */
    pinyin_ime->cand_btnm = lv_btnmatrix_create(pinyin_ime->cand_panel);
    lv_btnmatrix_set_map(pinyin_ime->cand_btnm, pinyin_ime->sel_map);
    lv_obj_set_height(pinyin_ime->cand_btnm, LV_PCT(100));
    lv_btnmatrix_set_one_checked(pinyin_ime->cand_btnm, true);
    lv_obj_clear_flag(pinyin_ime->cand_btnm, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    /* 候选按钮样式 */
    lv_obj_set_style_bg_opa(pinyin_ime->cand_btnm, LV_OPA_0, 0);
    lv_obj_set_style_border_width(pinyin_ime->cand_btnm, 0, 0);
    lv_obj_set_style_pad_all(pinyin_ime->cand_btnm, 0, 0);
    lv_obj_set_style_pad_row(pinyin_ime->cand_btnm, 0, 0);
    lv_obj_set_style_pad_column(pinyin_ime->cand_btnm, 0, 0);
    lv_obj_set_style_radius(pinyin_ime->cand_btnm, 12, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(pinyin_ime->cand_btnm, lv_color_hex(0xffffff), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(pinyin_ime->cand_btnm, LV_OPA_0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_opa(pinyin_ime->cand_btnm, LV_OPA_0, LV_PART_ITEMS);
    lv_obj_set_style_text_color(pinyin_ime->cand_btnm, lv_color_hex(0x222222), LV_PART_ITEMS);
    lv_obj_set_style_pad_left(pinyin_ime->cand_btnm, 0, LV_PART_ITEMS);
    lv_obj_set_style_pad_right(pinyin_ime->cand_btnm, 0, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(pinyin_ime->cand_btnm, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(pinyin_ime->cand_btnm, lv_color_hex(0xffffff), LV_PART_ITEMS | LV_STATE_PRESSED);

    /* event handler: 用 CLICKED(松手且未拖动才触发)而非 VALUE_CHANGED(按下即触发),
       这样拖动候选栏滚动时松手不会误选, 只有干净点按才上屏. */
    lv_obj_add_event_cb(pinyin_ime->cand_btnm, lv_ime_pinyin_cand_panel_event, LV_EVENT_CLICKED, obj);
    lv_obj_add_event_cb(obj, lv_ime_pinyin_style_change_event, LV_EVENT_STYLE_CHANGED, NULL);

#if LV_IME_PINYIN_USE_K9_MODE
    pinyin_ime->k9_input_str_len = 0;
    pinyin_ime->k9_py_ll_pos = 0;
    pinyin_ime->k9_legal_py_count = 0;
    lv_memset_00(pinyin_ime->k9_input_str, LV_IME_PINYIN_K9_MAX_INPUT);

    pinyin_k9_init_data(obj);

    _lv_ll_init(&(pinyin_ime->k9_legal_py_ll), sizeof(ime_pinyin_k9_py_str_t));
#endif
}


static void lv_ime_pinyin_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);

    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    if(lv_obj_is_valid(pinyin_ime->kb))
        lv_obj_del(pinyin_ime->kb);

    if(lv_obj_is_valid(pinyin_ime->cand_panel))
        lv_obj_del(pinyin_ime->cand_panel);
}


/* 26 键自动音节分隔 (仅自定义 jp_26 键盘): 当前末段字母 + 新字母不再是任何音节前缀时,
   就在新字母前插一个 ' (如 nih -> ni'h, women -> wo'men)。这些 ' 只用于输入栏显示,
   匹配/整句 DP 使用去掉 ' 的纯字母串; 用户按过分隔键后 (manual_sep) 不再自动插。 */
static bool pinyin_auto_sep_needed(lv_obj_t * obj, const char * input, uint16_t len, char ch)
{
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    if(pinyin_ime->manual_sep) return false;
    if(lv_obj_check_type(pinyin_ime->kb, &lv_keyboard_class)) return false;   /* 标准键盘无法同步显示 */
    if(len == 0 || input[len - 1] == '\'') return false;
    if(len + 2 >= sizeof(pinyin_ime->input_char)) return false;

    int seg_start = (int)len - 1;
    while(seg_start > 0 && input[seg_start - 1] != '\'') seg_start--;
    int sl = (int)len - seg_start;
    if(sl <= 0 || sl >= 7) return false;   /* 单音节最长 6 字母 */

    char seg[8];
    lv_memcpy(seg, input + seg_start, (size_t)sl);
    seg[sl] = ch;
    seg[sl + 1] = '\0';

    uint16_t dummy_num = 0;
    return pinyin_search_matching(obj, seg, &dummy_num) == NULL;
}


static void lv_ime_pinyin_kb_event(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * kb = lv_event_get_target(e);
    lv_obj_t * obj = lv_event_get_user_data(e);

    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

#if LV_IME_PINYIN_USE_K9_MODE
    static const char * k9_py_map[8] = {"abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz"};
#endif

    static bool long_pressed = false;

    if(code == LV_EVENT_LONG_PRESSED) {
        uint16_t btn_id = lv_btnmatrix_get_selected_btn(kb);
        if(btn_id == LV_BTNMATRIX_BTN_NONE) return;
        const char * txt = lv_btnmatrix_get_btn_text(kb, btn_id);
        if(txt == NULL) return;
        if(!pinyin_ime->k26_pinyin && txt[0] >= 'a' && txt[0] <= 'z' && txt[1] == '\0') {
            lv_textarea_add_char(pinyin_ime->ta, txt[0] - 'a' + 'A');
            long_pressed = true;
        }
        return;
    }

    if(code == LV_EVENT_VALUE_CHANGED) {
        if(long_pressed) {
            long_pressed = false;
            return;
        }
        uint16_t btn_id  = lv_btnmatrix_get_selected_btn(kb);
        if(btn_id == LV_BTNMATRIX_BTN_NONE) return;

        const char * txt = lv_btnmatrix_get_btn_text(kb, lv_btnmatrix_get_selected_btn(kb));
        if(txt == NULL) return;

#if LV_IME_PINYIN_USE_K9_MODE
        if(pinyin_ime->mode == LV_IME_PINYIN_MODE_K9) {
            lv_obj_t * ta = pinyin_ime->ta;
            uint16_t tmp_btn_str_len = strlen(pinyin_ime->input_char);
            if((btn_id >= 16) && (tmp_btn_str_len > 0) && (btn_id < (16 + LV_IME_PINYIN_K9_CAND_TEXT_NUM))) {
                tmp_btn_str_len = strlen(pinyin_ime->input_char);
                lv_memset_00(pinyin_ime->input_char, sizeof(pinyin_ime->input_char));
                strcat(pinyin_ime->input_char, txt);
                pinyin_input_proc(obj);

                for(int index = 0; index < (pinyin_ime->ta_count + tmp_btn_str_len); index++) {
                    lv_textarea_del_char(ta);
                }

                pinyin_ime->ta_count = tmp_btn_str_len;
                pinyin_ime->k9_input_str_len = tmp_btn_str_len;
                lv_textarea_add_text(ta, pinyin_ime->input_char);

                return;
            }
        }
#endif

        /* --- JP_26 custom keyboard: special buttons --- */
        if(strcmp(txt, "E    &   C") == 0 || strcmp(txt, "E    &   C") == 0) {
            pinyin_ime->k26_pinyin = !pinyin_ime->k26_pinyin;
            pinyin_ime_clear_data(obj);
            return;
        }
        if(strcmp(txt, "________") == 0) {
            /* 26键拼音模式: 空格键兼作手动分隔符 ' (如 xi'an), 按钮文案仍显示 "________" 不变.
               英文模式或空闲时仍作普通空格上屏. */
            if(pinyin_ime->k26_pinyin && pinyin_ime->ta_count > 0 &&
               pinyin_ime->ta_count < sizeof(pinyin_ime->input_char) - 1 &&
               pinyin_ime->input_char[pinyin_ime->ta_count - 1] != '\'') {
                pinyin_ime->input_char[pinyin_ime->ta_count] = '\'';
                pinyin_ime->ta_count++;
                pinyin_ime->input_char[pinyin_ime->ta_count] = '\0';
                /* 分隔须有整词匹配, 否则撤销 (同 9 键 on_an1) */
                if(!pinyin_segmented_has_prefix(obj, pinyin_ime->input_char)) {
                    pinyin_ime->ta_count--;
                    pinyin_ime->input_char[pinyin_ime->ta_count] = '\0';
                    return;
                }
                pinyin_ime->manual_sep = 1;   /* 用户显式分隔: 之后按分段匹配, 不再自动插 ' */
                if(!lv_obj_check_type(pinyin_ime->kb, &lv_keyboard_class)) {
                    lv_textarea_add_text(pinyin_ime->ta, "'");
                }
                pinyin_input_proc(obj);
                return;
            }
            lv_textarea_add_char(pinyin_ime->ta, ' ');
            return;
        }
        if(strcmp(txt, "<") == 0) {
            lv_textarea_cursor_left(pinyin_ime->ta);
            return;
        }
        if(strcmp(txt, ">") == 0) {
            lv_textarea_cursor_right(pinyin_ime->ta);
            return;
        }
        if(strcmp(txt, "?") == 0) {
            lv_textarea_add_char(pinyin_ime->ta, '?');
            return;
        }
        if(strcmp(txt, "!") == 0) {
            lv_textarea_add_char(pinyin_ime->ta, '!');
            return;
        }
        if(strcmp(txt, ",") == 0) {
            lv_textarea_add_char(pinyin_ime->ta, ',');
            return;
        }
        if(strcmp(txt, ".") == 0) {
            lv_textarea_add_char(pinyin_ime->ta, '.');
            return;
        }
        if(txt[0] >= '0' && txt[0] <= '9' && txt[1] == '\0') {
            lv_textarea_add_char(pinyin_ime->ta, txt[0]);
            return;
        }

        if(strcmp(txt, "Enter") == 0 || strcmp(txt, LV_SYMBOL_NEW_LINE) == 0 || strcmp(txt, "←┛") == 0 || strcmp(txt, "发 送") == 0) {
            pinyin_ime_clear_data(obj);
            lv_obj_add_flag(pinyin_ime->cand_panel, LV_OBJ_FLAG_HIDDEN);
            lv_textarea_add_char(pinyin_ime->ta, '\n');
        }
        else if(strcmp(txt, "←") == 0 || strcmp(txt, LV_SYMBOL_BACKSPACE) == 0 || strcmp(txt, "退格") == 0) {
            // del input char
            bool ate_sep = false;   /* 退格时末尾自动分隔符 ' 被一并删除 */
            if(pinyin_ime->ta_count > 0) {
                if(pinyin_ime->mode == LV_IME_PINYIN_MODE_K26)
                    pinyin_ime->input_char[pinyin_ime->ta_count - 1] = '\0';
#if LV_IME_PINYIN_USE_K9_MODE
                else
                    pinyin_ime->k9_input_str[pinyin_ime->ta_count - 1] = '\0';
#endif

                pinyin_ime->ta_count = pinyin_ime->ta_count - 1;
                /* 末尾只剩自动音节分隔 ' 时一并吞掉 (手动分隔保留: 用户显式按的) */
                if(pinyin_ime->mode == LV_IME_PINYIN_MODE_K26 && !pinyin_ime->manual_sep &&
                   pinyin_ime->ta_count > 0 &&
                   pinyin_ime->input_char[pinyin_ime->ta_count - 1] == '\'') {
                    pinyin_ime->ta_count = pinyin_ime->ta_count - 1;
                    pinyin_ime->input_char[pinyin_ime->ta_count] = '\0';
                    ate_sep = true;
                }
                if(pinyin_ime->ta_count <= 0) {
                    lv_obj_add_flag(pinyin_ime->cand_panel, LV_OBJ_FLAG_HIDDEN);
#if LV_IME_PINYIN_USE_K9_MODE
                    pinyin_ime->k9_input_str_len = 0;
                    lv_memset_00(lv_pinyin_k9_cand_str, sizeof(lv_pinyin_k9_cand_str));
                    strcpy(lv_pinyin_k9_cand_str[LV_IME_PINYIN_K9_CAND_TEXT_NUM], LV_SYMBOL_RIGHT"\0");
                    strcpy(lv_pinyin_k9_cand_str[LV_IME_PINYIN_K9_CAND_TEXT_NUM + 1], "\0");
#endif
                }
                else if(pinyin_ime->mode == LV_IME_PINYIN_MODE_K26) {
                    pinyin_input_proc(obj);
                }
#if LV_IME_PINYIN_USE_K9_MODE
                else if(pinyin_ime->mode == LV_IME_PINYIN_MODE_K9) {
                    pinyin_ime->k9_input_str_len = 0;
                    for(uint8_t j = 0; j < pinyin_ime->ta_count; j++) {
                        uint8_t key = pinyin_ime->k9_input_str[j];
                        if(key >= '2' && key <= '9') {
                            pinyin_ime->k9_input_str_len += strlen(k9_py_map[key - '2']);
                        }
                    }
                    pinyin_k9_get_legal_py(obj, pinyin_ime->k9_input_str, k9_py_map);
                    pinyin_k9_fill_cand(obj);
                    pinyin_input_proc(obj);
                }
#endif
            }
            /* Custom btnmatrix: always delete from textarea (standard keyboard does this automatically) */
            if(!lv_obj_check_type(pinyin_ime->kb, &lv_keyboard_class)) {
                lv_textarea_del_char(pinyin_ime->ta);
                if(ate_sep) lv_textarea_del_char(pinyin_ime->ta);   /* 自动分隔符也是我们加的, 再删一个 */
            }
        }
        else if((strcmp(txt, "ABC") == 0) || (strcmp(txt, "abc") == 0) || (strcmp(txt, "1#") == 0) || (strcmp(txt, "123") == 0)) {
            pinyin_ime->ta_count = 0;
            pinyin_ime->manual_sep = 0;
            lv_memset_00(pinyin_ime->input_char, sizeof(pinyin_ime->input_char));
            return;
        }
        else if(strcmp(txt, LV_SYMBOL_KEYBOARD) == 0) {
            if(pinyin_ime->mode == LV_IME_PINYIN_MODE_K26) {
                lv_ime_pinyin_set_mode(pinyin_ime, LV_IME_PINYIN_MODE_K9);
            }
            else {
                lv_ime_pinyin_set_mode(pinyin_ime, LV_IME_PINYIN_MODE_K26);
                lv_keyboard_set_mode(pinyin_ime->kb, LV_KEYBOARD_MODE_TEXT_LOWER);
            }
            pinyin_ime_clear_data(obj);
        }
        else if(strcmp(txt, LV_SYMBOL_OK) == 0) {
            pinyin_ime_clear_data(obj);
        }
        else if((pinyin_ime->mode == LV_IME_PINYIN_MODE_K26) && ((txt[0] >= 'a' && txt[0] <= 'z') || (txt[0] >= 'A' &&
                                                                                                      txt[0] <= 'Z'))) {
            /* 自动拦截 (与 9 键 k9_pinyin 一致): 拼音模式下, 新字母若使序列既无单音节前缀、也无整词前缀匹配, 忽略该键
               (不加入 textarea/不累积), 保持当前候选与选字栏不消失 */
            /* 输入上限检查提前到写 textarea 之前: 原来先 add_text 再判断, 达到上限时
               textarea 多出字母而 input_char 不累积, 两处内容脱节 */
            if(pinyin_ime->k26_pinyin && (pinyin_ime->ta_count >= sizeof(pinyin_ime->input_char) - 1)) {
                return;
            }
            if(pinyin_ime->k26_pinyin) {
                char ch = txt[0];
                if(ch >= 'A' && ch <= 'Z') ch += ('a' - 'A');
                char tmp[sizeof(pinyin_ime->input_char)];
                lv_memset_00(tmp, sizeof(tmp));
                lv_memcpy(tmp, pinyin_ime->input_char, pinyin_ime->ta_count);
                tmp[pinyin_ime->ta_count] = ch;
                bool valid;
                if(pinyin_ime->manual_sep) {
                    valid = pinyin_segmented_has_prefix(obj, tmp);   /* 带手动分隔: 用分段整词前缀 */
                }
                else {
                    /* 自动音节分隔 ' 只用于显示: 判断前先去掉 */
                    char plain[sizeof(tmp)];
                    uint16_t pw = 0;
                    for(uint16_t i = 0; tmp[i] != '\0' && pw + 1 < sizeof(plain); i++) {
                        if(tmp[i] != '\'') plain[pw++] = tmp[i];
                    }
                    plain[pw] = '\0';
                    uint16_t dummy_num = 0;
                    valid = (pinyin_search_matching(obj, plain, &dummy_num) != NULL) ||
                            pinyin_phrase_has_prefix(obj, plain) ||
                            (pinyin_ime->sentence_enable && pinyinDpCanExtend(plain, false));
                }
                if(!valid) {
                    return;   // 无效序列 → 忽略该键
                }
            }
            /* 拼音模式: 累积输入并同步 textarea (自动音节分隔 ' 仅自定义键盘显示).
               标准键盘由 lv_keyboard 自己加字母, 只累积不插分隔. */
            if(pinyin_ime->k26_pinyin) {
                if(pinyin_ime->ta_count < sizeof(pinyin_ime->input_char) - 1) {
                    bool custom_kb = !lv_obj_check_type(pinyin_ime->kb, &lv_keyboard_class);
                    char ch = txt[0];
                    if(ch >= 'A' && ch <= 'Z') ch += ('a' - 'A');
                    if(pinyin_auto_sep_needed(obj, pinyin_ime->input_char, pinyin_ime->ta_count, ch)) {
                        pinyin_ime->input_char[pinyin_ime->ta_count] = '\'';
                        pinyin_ime->ta_count++;
                        lv_textarea_add_text(pinyin_ime->ta, "'");
                    }
                    pinyin_ime->input_char[pinyin_ime->ta_count] = ch;
                    pinyin_ime->ta_count++;
                    pinyin_ime->input_char[pinyin_ime->ta_count] = '\0';   /* 匹配/DP 用 strlen, 必须终止 */
                    if(custom_kb) lv_textarea_add_text(pinyin_ime->ta, txt);
                    pinyin_input_proc(obj);
                }
            }
            else if(!lv_obj_check_type(pinyin_ime->kb, &lv_keyboard_class)) {
                lv_textarea_add_text(pinyin_ime->ta, txt);   /* 英文模式: 直接上屏 */
            }
            return;
        }
#if LV_IME_PINYIN_USE_K9_MODE
        else if((pinyin_ime->mode == LV_IME_PINYIN_MODE_K9) && (txt[0] >= 'a' && txt[0] <= 'z')) {
            for(uint16_t i = 0; i < 8; i++) {
                if((strcmp(txt, k9_py_map[i]) == 0) || (strcmp(txt, "abc ") == 0)) {
                    if(strcmp(txt, "abc ") == 0)    pinyin_ime->k9_input_str_len += strlen(k9_py_map[i]) + 1;
                    else                            pinyin_ime->k9_input_str_len += strlen(k9_py_map[i]);
                    pinyin_ime->k9_input_str[pinyin_ime->ta_count] = 50 + i;
                    pinyin_ime->ta_count++;  /* increment after storing k9 input */
                    break;
                }
            }
            pinyin_k9_get_legal_py(obj, pinyin_ime->k9_input_str, k9_py_map);
            pinyin_k9_fill_cand(obj);
            pinyin_input_proc(obj);
        }
        else if(strcmp(txt, LV_SYMBOL_LEFT) == 0) {
            pinyin_k9_cand_page_proc(obj, 0);
        }
        else if(strcmp(txt, LV_SYMBOL_RIGHT) == 0) {
            pinyin_k9_cand_page_proc(obj, 1);
        }
#endif
    }
}


static void lv_ime_pinyin_cand_panel_event(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * cand_btnm = lv_event_get_target(e);
    lv_obj_t * obj = (lv_obj_t *)lv_event_get_user_data(e);

    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    if(code == LV_EVENT_CLICKED) {
        uint32_t id = lv_btnmatrix_get_selected_btn(cand_btnm);
        if(id == LV_BTNMATRIX_BTN_NONE) return;

        const char * txt = lv_btnmatrix_get_btn_text(cand_btnm, id);

        /* 选候选(整词或单字) → 记频自学习. 整句候选(第一位)不参与学习, 不写入词频表 */
        if(txt && txt[0] && pinyin_ime->word_note_cb &&
           !(id == 0 && pinyin_ime->has_sentence)) {
            pinyin_ime->word_note_cb(txt);
        }

        lv_obj_t * ta = pinyin_ime->ta;
        uint16_t index = 0;

        /* 前缀候选: 只消耗输入的前 consume 个字母, 余下字母继续输入.
           consume 以「纯字母串(keys)」计, 而 input_char/textarea 里含自动分隔 ', 需要换算。 */
        uint16_t ci = (uint16_t)(id - (pinyin_ime->has_sentence ? 1 : 0));
        uint8_t consume = 0;
        if(!(id == 0 && pinyin_ime->has_sentence) && ci < pinyin_ime->cand_num &&
           ci < LV_PHRASE_CAND_MAX)
            consume = pinyin_ime->cand_consume[ci];

        if(consume > 0) {
            uint16_t dl = 0, letters = 0;
            while(pinyin_ime->input_char[dl] && letters < consume) {
                if(pinyin_ime->input_char[dl] != '\'') letters++;
                dl++;
            }
            if(letters == consume && dl < pinyin_ime->ta_count) {
                for(index = 0; index < pinyin_ime->ta_count; index++)
                    lv_textarea_del_char(ta);
                lv_textarea_add_text(ta, txt);
                while(pinyin_ime->input_char[dl] == '\'') dl++;   /* 剩余串开头的自动分隔已无意义 */
                uint16_t rest = (uint16_t)strlen(pinyin_ime->input_char + dl);
                memmove(pinyin_ime->input_char, pinyin_ime->input_char + dl, rest + 1);
                pinyin_ime->ta_count = rest;
                if(rest > 0) {
                    lv_textarea_add_text(ta, pinyin_ime->input_char);   /* 余下拼音继续显示 */
                    pinyin_input_proc(obj);                             /* 按剩余字母重算候选 */
                }
                else {
                    pinyin_ime_clear_data(obj);
                }
                return;
            }
        }

        for(index = 0; index < pinyin_ime->ta_count; index++)
            lv_textarea_del_char(ta);

        lv_textarea_add_text(ta, txt);

        pinyin_ime_clear_data(obj);
    }
}

static void pinyin_input_proc(lv_obj_t * obj)
{
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    uint16_t total = 0;
    /* 只有用户显式按过分隔键才算「带分隔」; 自动音节分隔 ' 仅用于输入栏显示,
       匹配/整句 DP 一律用去掉 ' 的纯字母串 (keys). */
    bool has_sep = (pinyin_ime->manual_sep != 0);
    char plain[sizeof(pinyin_ime->input_char)];
    const char * keys = pinyin_ime->input_char;
    if(!has_sep) {
        uint16_t pw = 0;
        const char * s = pinyin_ime->input_char;
        while(*s && pw + 1 < sizeof(plain)) {
            if(*s != '\'') plain[pw++] = *s;
            s++;
        }
        plain[pw] = '\0';
        keys = plain;
    }
    uint16_t input_len = (uint16_t)strlen(keys);
    bool show_words = false;
    /* 前缀候选(分词/分字)是否可用: 无手动分隔且有词库. 与整词/单字候选并存, 行为对齐 9 键:
       候选栏 = 整句 DP + 整词 + 前缀词(分词) + 首音节单字(分字). */
    bool pfx_ok = (!has_sep && pinyin_ime->phrase_dict != NULL && input_len >= 2);
    lv_memset_00(pinyin_ime->cand_consume, sizeof(pinyin_ime->cand_consume));

    /* 1. 整词候选 (前缀匹配: 输入是词拼音的前缀, 精确匹配是其子集).
          仅在"已跨过音节边界"时才出整词, 保证单音节 "ni"/"zhong" 仍出单字而非被整词刷屏;
          "nih"/"niha"/"nihao" 都会命中 "你好". 带手动分隔 ' 时改用分段匹配(xi'an 与 xian 区分).
          2.5a 的前缀词候选也在同一趟桶扫描里收集, 避免对同一首字母桶扫两遍. */
    bool wb = (pinyin_ime->phrase_dict != NULL && input_len >= 2 &&
               (has_sep || pinyin_is_multisyllable(obj, (char *)keys)));
    /* 前缀词只输出前 8 条, 用有界插入维持 (频次降序, 词库下标升序):
       词库下标唯一 → 排序键为全序, 与「全收集后排序取前 8」结果完全一致. */
    uint16_t pfx_idx[8];
    uint16_t pfx_score[8];
    uint8_t pfx_n = 0;
    if(wb || pfx_ok) {
        uint8_t offset = pinyin_ime->input_char[0] - 'a';
        if(offset < 26) {
            uint16_t count = pinyin_ime->phrase_num[offset];
            const lv_phrase_dict_t * p = &pinyin_ime->phrase_dict[pinyin_ime->phrase_pos[offset]];
            uint16_t m = 0;
            while(count--) {
                uint16_t dict_idx = (uint16_t)(p - pinyin_ime->phrase_dict);
                if(wb) {
                    bool match = has_sep ?
                                 pinyin_segmented_match(obj, p->py, p->word, keys) :
                                 (strncmp(p->py, keys, input_len) == 0);
                    if(match && m < LV_PHRASE_MATCH_MAX) {
                        pinyin_ime->cand_idx[m] = dict_idx;
                        pinyin_ime->cand_score[m] = pinyin_ime->word_score_cb ?
                                                    pinyin_ime->word_score_cb(p->word) : 0;
                        m++;
                    }
                }
                if(pfx_ok) {
                    /* 2.5a 分词: 词拼音是输入的完整前缀, 点选只消耗该词字母 */
                    uint16_t L = (uint16_t)strlen(p->py);
                    /* 1 字词条跳过: 首音节单字由 2.5b 提供, 避免候选重复 */
                    if(L > 0 && L < input_len && p->word[3] != '\0' &&
                       strncmp(keys, p->py, L) == 0) {
                        uint16_t sc = pinyin_ime->word_score_cb ?
                                      pinyin_ime->word_score_cb(p->word) : 0;
                        int pos = pfx_n;
                        while(pos > 0 && (pfx_score[pos - 1] < sc ||
                                          (pfx_score[pos - 1] == sc && pfx_idx[pos - 1] > dict_idx))) pos--;
                        if(pos < 8) {
                            int end = (pfx_n < 8) ? pfx_n : 7;
                            for(int q = end; q > pos; q--) {
                                pfx_idx[q] = pfx_idx[q - 1];
                                pfx_score[q] = pfx_score[q - 1];
                            }
                            pfx_idx[pos] = dict_idx;
                            pfx_score[pos] = sc;
                            if(pfx_n < 8) pfx_n++;
                        }
                    }
                }
                p++;
            }
            if(wb) {
                /* 稳定排序: 用户频次降序, 同频次按词库下标升序(= 静态词频降序) */
                for(uint16_t i = 1; i < m; i++) {
                    uint16_t v = pinyin_ime->cand_idx[i];
                    uint16_t sv = pinyin_ime->cand_score[i];
                    int j = (int)i - 1;
                    while(j >= 0 && (pinyin_ime->cand_score[j] < sv ||
                                     (pinyin_ime->cand_score[j] == sv && pinyin_ime->cand_idx[j] > v))) {
                        pinyin_ime->cand_idx[j + 1] = pinyin_ime->cand_idx[j];
                        pinyin_ime->cand_score[j + 1] = pinyin_ime->cand_score[j];
                        j--;
                    }
                    pinyin_ime->cand_idx[j + 1] = v;
                    pinyin_ime->cand_score[j + 1] = sv;
                }
                /* 给前缀候选 (≤8 词 + ≤6 字) 和整句 DP 留位置 */
                uint16_t max_take = pfx_ok ? (LV_PHRASE_CAND_MAX - 15) : LV_PHRASE_CAND_MAX;
                uint16_t take = m < max_take ? m : max_take;
                for(uint16_t i = 0; i < take; i++) {
                    strcpy(pinyin_ime->cand_word[total], pinyin_ime->phrase_dict[pinyin_ime->cand_idx[i]].word);
                    total++;
                }
            }
        }
        if(wb) show_words = (total > 0);
    }

    /* 2. 无整词候选时退回单字候选 (沿用前缀匹配: "n"→那哪拿, "zhong"→中种终).
          出整词时不混单字, 满足"输入多个字就不出现单字选项". 带分隔时退回「末段」单字(正在输入的音节). */
    if(!show_words) {
        const char * seg = keys;
        if(has_sep) {
            const char * last = strrchr(pinyin_ime->input_char, '\'');
            seg = last ? (last + 1) : pinyin_ime->input_char;
        }
        uint16_t syl_num = 0;
        char * syl_mb = pinyin_search_matching(obj, (char *)seg, &syl_num);
        if(syl_mb != NULL) {
            /* 单字候选也按用户频次自学习排序: 收集 (字内位置=静态字频序, 频次) → 稳定排序 */
            uint16_t n = syl_num < LV_PHRASE_MATCH_MAX ? syl_num : LV_PHRASE_MATCH_MAX;
            for(uint16_t i = 0; i < n; i++) {
                char ch[4];
                ch[0] = syl_mb[i * 3];
                ch[1] = syl_mb[i * 3 + 1];
                ch[2] = syl_mb[i * 3 + 2];
                ch[3] = '\0';
                pinyin_ime->cand_idx[i] = i;
                pinyin_ime->cand_score[i] = pinyin_ime->word_score_cb ?
                                            pinyin_ime->word_score_cb(ch) : 0;
            }
            for(uint16_t i = 1; i < n; i++) {
                uint16_t v = pinyin_ime->cand_idx[i];
                uint16_t sv = pinyin_ime->cand_score[i];
                int j = (int)i - 1;
                while(j >= 0 && (pinyin_ime->cand_score[j] < sv ||
                                 (pinyin_ime->cand_score[j] == sv && pinyin_ime->cand_idx[j] > v))) {
                    pinyin_ime->cand_idx[j + 1] = pinyin_ime->cand_idx[j];
                    pinyin_ime->cand_score[j + 1] = pinyin_ime->cand_score[j];
                    j--;
                }
                pinyin_ime->cand_idx[j + 1] = v;
                pinyin_ime->cand_score[j + 1] = sv;
            }
            for(uint16_t i = 0; i < n && total < LV_PHRASE_CAND_MAX; i++) {
                uint16_t k = pinyin_ime->cand_idx[i];
                pinyin_ime->cand_word[total][0] = syl_mb[k * 3];
                pinyin_ime->cand_word[total][1] = syl_mb[k * 3 + 1];
                pinyin_ime->cand_word[total][2] = syl_mb[k * 3 + 2];
                pinyin_ime->cand_word[total][3] = '\0';
                total++;
            }
        }
    }

    /* 2.5 前缀候选 (与 9 键同款, 和整词/整句并存):
           a) 分词: 词拼音是输入的完整前缀 (中文 ⊂ zhongwenpinyin)。点选后只消耗该词对应的字母,
              余下字母继续输入, 用来「先点中文, 再选拼音」. 收集已并入上面的桶扫描 (pfx_idx).
           b) 分字: 首音节的单字 (zhong → 中/种/重…), 消耗该音节字母数, 用来逐字上屏.
           这也兜住了末音节没输完 (zhongwenp) 时整词/单字/整句都不成立、候选栏整个消失的问题。 */
    if(pfx_ok) {
        for(uint8_t i = 0; i < pfx_n && total < LV_PHRASE_CAND_MAX; i++) {
            const lv_phrase_dict_t * q = &pinyin_ime->phrase_dict[pfx_idx[i]];
            strcpy(pinyin_ime->cand_word[total], q->word);
            pinyin_ime->cand_consume[total] = (uint8_t)strlen(q->py);
            total++;
        }
        /* 2.5b 分字: 首音节单字 (zhong → 中/种/终/钟…). 单音节输入时 slen == input_len, 不出. */
        uint16_t syl_num = 0;
        uint8_t syl_len = 0;
        char * syl_mb = pinyin_longest_syllable_prefix(obj, (char *)keys, &syl_num, &syl_len);
        if(syl_mb != NULL && syl_len > 0 && syl_len < input_len) {
            uint16_t n = syl_num < 6 ? syl_num : 6;   /* 最多 6 个, 避免刷屏 */
            for(uint16_t i = 0; i < n && total < LV_PHRASE_CAND_MAX; i++) {
                pinyin_ime->cand_word[total][0] = syl_mb[i * 3];
                pinyin_ime->cand_word[total][1] = syl_mb[i * 3 + 1];
                pinyin_ime->cand_word[total][2] = syl_mb[i * 3 + 2];
                pinyin_ime->cand_word[total][3] = '\0';
                pinyin_ime->cand_consume[total] = syl_len;
                total++;
            }
        }
    }

    /* 3. 整句 DP: 连续全拼 (无分隔, 26 键) 时求全局最优切分, 作为候选栏第一项.
          只有 >=2 条边 (= 跨词整句) 且汉字数不超限才展示; 单音节/单词不额外加项. */
    pinyin_ime->has_sentence = 0;
    pinyin_ime->sentence_chars = 0;
    if(pinyin_ime->sentence_enable && !has_sep && pinyin_ime->k26_pinyin &&
       pinyin_ime->mode == LV_IME_PINYIN_MODE_K26 && input_len >= 2) {
        pinyin_dp_result_t dp;
        if(pinyinDpSolve(keys, false, NULL, &dp) &&
           dp.edges >= 2 && dp.chars >= 2 && dp.chars <= PINYIN_DP_MAX_CHARS) {
            lv_memcpy(pinyin_ime->sentence, dp.text, sizeof(dp.text));
            pinyin_ime->sentence_chars = dp.chars;
            pinyin_ime->has_sentence = 1;
            if(total >= LV_PHRASE_CAND_MAX) total = LV_PHRASE_CAND_MAX - 1;
        }
    }

    pinyin_ime->cand_num = total + pinyin_ime->has_sentence;
    if(pinyin_ime->cand_num == 0) {
        lv_obj_add_flag(pinyin_ime->cand_panel, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    /* 横向滚动候选栏: 一次性列出全部候选, 不分页/无箭头. 每键重建 btnmatrix map +
       按字数等比设宽, 并回卷到最左. */
    pinyin_build_cand_map(obj);
    lv_obj_clear_flag(pinyin_ime->cand_panel, LV_OBJ_FLAG_HIDDEN);
}


static void lv_ime_pinyin_style_change_event(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);

    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    if(code == LV_EVENT_STYLE_CHANGED) {
        const lv_font_t * font = lv_obj_get_style_text_font(obj, LV_PART_MAIN);
        lv_obj_set_style_text_font(pinyin_ime->cand_btnm, font, 0);
    }
}


static void init_pinyin_dict(lv_obj_t * obj, lv_pinyin_dict_t * dict)
{
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    char headletter = 'a';
    uint16_t offset_sum = 0;
    uint16_t offset_count = 0;
    uint16_t letter_calc = 0;

    pinyin_ime->dict = dict;

    for(uint16_t i = 0; ; i++) {
        if((NULL == (dict[i].py)) || (NULL == (dict[i].py_mb))) {
            headletter = dict[i - 1].py[0];
            letter_calc = headletter - 'a';
            pinyin_ime->py_num[letter_calc] = offset_count;
            break;
        }

        if(headletter == (dict[i].py[0])) {
            offset_count++;
        }
        else {
            letter_calc = headletter - 'a';
            pinyin_ime->py_num[letter_calc] = offset_count;
            offset_sum += offset_count;
            headletter = dict[i].py[0];
            pinyin_ime->py_pos[headletter - 'a'] = offset_sum;

            offset_count = 1;
        }
    }
}

static void init_phrase_dict(lv_obj_t * obj, const lv_phrase_dict_t * dict)
{
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    pinyin_ime->phrase_dict = dict;
    lv_memset_00(pinyin_ime->phrase_num, sizeof(pinyin_ime->phrase_num));
    lv_memset_00(pinyin_ime->phrase_pos, sizeof(pinyin_ime->phrase_pos));
    if(dict == NULL) return;

    char headletter = 'a';
    uint16_t offset_sum = 0;
    uint16_t offset_count = 0;
    uint16_t letter_calc = 0;

    for(uint16_t i = 0; ; i++) {
        if((NULL == dict[i].py) || (NULL == dict[i].word)) {
            headletter = dict[i - 1].py[0];
            letter_calc = headletter - 'a';
            pinyin_ime->phrase_num[letter_calc] = offset_count;
            break;
        }

        if(headletter == dict[i].py[0]) {
            offset_count++;
        }
        else {
            letter_calc = headletter - 'a';
            pinyin_ime->phrase_num[letter_calc] = offset_count;
            offset_sum += offset_count;
            headletter = dict[i].py[0];
            pinyin_ime->phrase_pos[headletter - 'a'] = offset_sum;
            offset_count = 1;
        }
    }
}


static char * pinyin_search_matching(lv_obj_t * obj, char * py_str, uint16_t * cand_num)
{
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    lv_pinyin_dict_t * cpHZ;
    uint8_t index, len = 0, offset;
    volatile uint8_t count = 0;

    if(*py_str == '\0')    return NULL;

    /* Convert to lowercase (fix crash on uppercase input) */
    for(uint8_t i = 0; py_str[i]; i++) {
        if(py_str[i] >= 'A' && py_str[i] <= 'Z') {
            py_str[i] += 32;
        }
    }

    /* 允许 "i"/"u" 作为符号候选 (与 "v" 相同, 词库已含 i/u 条目, 勿再拦截) */
    offset = py_str[0] - 'a';
    len = strlen(py_str);

    cpHZ  = &pinyin_ime->dict[pinyin_ime->py_pos[offset]];
    count = pinyin_ime->py_num[offset];

    while(count--) {
        for(index = 0; index < len; index++) {
            if(*(py_str + index) != *((cpHZ->py) + index)) {
                break;
            }
        }

        // perfect match
        if(len == 1 || index == len) {
            // The Chinese character in UTF-8 encoding format is 3 bytes
            * cand_num = strlen((const char *)(cpHZ->py_mb)) / 3;
            return (char *)(cpHZ->py_mb);
        }
        cpHZ++;
    }
    return NULL;
}

/* 在单音节词库中找以 py_str 开头的最长完整音节, 返回候选字串; syl_len 传出音节长度.
   优先匹配更长音节(如 "zhong" 优于 "zho"不存在 -> 匹配 "zhuang" 前的 zhu 等), 供贪心切分用 */
static char * pinyin_longest_syllable_prefix(lv_obj_t * obj, char * py_str, uint16_t * cand_num, uint8_t * syl_len)
{
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;
    if(*py_str == '\0') return NULL;
    uint8_t offset = py_str[0] - 'a';
    if(offset >= 26) return NULL;
    uint16_t count = pinyin_ime->py_num[offset];
    lv_pinyin_dict_t * cpHZ = &pinyin_ime->dict[pinyin_ime->py_pos[offset]];
    char * best = NULL;
    uint8_t best_len = 0;
    while(count--) {
        if(cpHZ->py_mb[0] != '\0') {
            uint8_t plen = (uint8_t)strlen((const char *)(cpHZ->py));
            if(strncmp(cpHZ->py, py_str, plen) == 0) {
                /* cpHZ->py 是 py_str 的完整音节前缀 */
                if(plen > best_len) {
                    best = (char *)(cpHZ->py_mb);
                    best_len = plen;
                    *cand_num = strlen((const char *)(cpHZ->py_mb)) / 3;
                }
            }
        }
        cpHZ++;
    }
    if(best != NULL) { *syl_len = best_len; return best; }
    return NULL;
}

/* 判断输入是否"已跨过音节边界"(即不止一个音节):
   从前往后贪心切分, 若切出 >=2 个完整音节, 或 >=1 个完整音节后还有剩余字母, 则视为多音节输入.
   用于决定是否出整词候选: "ni"/"zhong" 是单音节 -> 出单字; "nih"/"nihao"/"zhongguo" -> 出整词. */
static bool pinyin_is_multisyllable(lv_obj_t * obj, char * py_str)
{
    int len = (int)strlen(py_str);
    int pos = 0;
    int syllables = 0;
    while(pos < len) {
        uint16_t n = 0;
        uint8_t slen = 0;
        char * mb = pinyin_longest_syllable_prefix(obj, py_str + pos, &n, &slen);
        if(mb == NULL) break;   /* 尾部是未完成音节 */
        syllables++;
        pos += slen;
    }
    return (syllables >= 2) || (syllables >= 1 && pos < len);
}

/* 判断 py_str 是否为某个整词拼音的前缀 (用于自动拦截: 允许继续输入多音节) */
static bool pinyin_phrase_has_prefix(lv_obj_t * obj, char * py_str)
{
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;
    if(pinyin_ime->phrase_dict == NULL || *py_str == '\0') return false;
    uint8_t offset = py_str[0] - 'a';
    if(offset >= 26) return false;
    uint16_t count = pinyin_ime->phrase_num[offset];
    lv_phrase_dict_t * p = &pinyin_ime->phrase_dict[pinyin_ime->phrase_pos[offset]];
    uint16_t len = (uint16_t)strlen(py_str);
    while(count--) {
        if(strncmp(p->py, py_str, len) == 0) return true;
        p++;
    }
    return false;
}

/* 判断 syl 是否为单音节词典里的完整音节 (py_mb 非空才是真实音节, 排除 "i"/"u"/"v" 占位条目) */
static bool pinyin_is_syllable(lv_obj_t * obj, const char * syl)
{
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;
    if(*syl == '\0') return false;
    uint8_t offset = syl[0] - 'a';
    if(offset >= 26) return false;
    /* 词典每个首字母桶内按拼音字母序排列 (custom_pinyin_dict 由生成脚本保证, 拼音无重复)
       → 二分查找; 手动分隔的每键匹配要查成千上万次, 线性扫会明显拖慢输入。 */
    int lo = (int)pinyin_ime->py_pos[offset];
    int hi = lo + (int)pinyin_ime->py_num[offset] - 1;
    while(lo <= hi) {
        int mid = (lo + hi) >> 1;
        int c = strcmp(pinyin_ime->dict[mid].py, syl);
        if(c == 0) return pinyin_ime->dict[mid].py_mb[0] != '\0';
        if(c < 0) lo = mid + 1;
        else hi = mid - 1;
    }
    return false;
}

/* 回溯把整词拼音 py 切成恰好 n 个完整音节 (每字一音节), 各音节长度写入 lens[0..n-1].
   仅用于分段匹配命中后的「剩余音节可切分」校验 (匹配阶段本身不再对整词切分)。 */
static bool pinyin_seg_syll_rec(lv_obj_t * obj, const char * py, int plen, int pos, int n, int * lens, int done)
{
    if(done == n) return (pos == plen);
    int remain = n - done;
    int maxL = plen - pos - (remain - 1);
    if(maxL > 6) maxL = 6;
    int minL = plen - pos - (remain - 1) * 6;
    if(minL < 1) minL = 1;
    for(int L = minL; L <= maxL; L++) {
        char syl[8];
        memcpy(syl, py + pos, L);
        syl[L] = '\0';
        if(!pinyin_is_syllable(obj, syl)) continue;
        lens[done] = L;
        if(pinyin_seg_syll_rec(obj, py, plen, pos + L, n, lens, done + 1)) return true;
    }
    return false;
}

static bool pinyin_segment_word_syllables(lv_obj_t * obj, const char * py, int n, int * lens)
{
    int plen = (int)strlen(py);
    if(n <= 0 || plen < n || plen > n * 6) return false;
    return pinyin_seg_syll_rec(obj, py, plen, 0, n, lens, 0);
}

/* 整词 py/word 是否匹配带手动分隔的输入 input (段以 ' 分隔; 完整段必须与词的真实音节边界对齐,
   末段(无尾 ')是当前音节前缀). 同 9 键 word_match_segmented, 用"每字一音节"区分 西安 xi|an 与 现在 xian|zai.

   性能: 只在「输入各段边界处」查音节, 不再先把整词拼音切分成 nchar 个音节。
   旧实现每词都跑 pinyin_segment_word_syllables (每个音节候选都要线性扫一遍单音节词典),
   手动分隔后每键要扫整个首字母桶 + 有效性预检再扫一遍 → 数万次词典查找, 是分隔后输入卡顿的根因。
   新实现用「可达音节边界位掩码」: 常见词在段内字母比较阶段就被否掉, 只有命中才查词典。 */
static bool pinyin_segmented_match(lv_obj_t * obj, const char * py, const char * word, const char * input)
{
    int in_len = (int)strlen(input);
    if(in_len <= 0) return false;

    /* 切分 input 的 ' 分隔段 */
    int nseg = 0, segstart[8], seglen[8];
    bool trailing_sep = (input[in_len - 1] == '\'');
    int i = 0;
    while(i < in_len) {
        if(input[i] == '\'') { i++; continue; }
        int j = i;
        while(j < in_len && input[j] != '\'') j++;
        if(nseg >= 8) return false;
        segstart[nseg] = i;
        seglen[nseg] = j - i;
        nseg++;
        i = j;
    }
    if(nseg <= 0) return false;

    int nchar = (int)strlen(word) / 3;   /* 词全 3 字节 UTF-8 汉字, 每字一音节 */
    int plen = (int)strlen(py);
    if(nchar <= 0 || nchar > 8 || plen < nchar || plen > nchar * 6 || plen > 63) return false;
    if(nseg > nchar) return false;       /* 输入段数不可能多于词的字数 */

    /* 位掩码 bit p = 字母位置 p 是可达的音节边界; 只推进输入覆盖到的段数 (前缀匹配) */
    uint64_t reach = 1;
    for(int seg = 0; seg < nseg; seg++) {
        int L = seglen[seg];
        bool full = (seg != nseg - 1) || trailing_sep;   /* 完整段必须是整音节 */
        uint64_t next = 0;
        for(int p = 0; p < plen; p++) {
            if(!((reach >> p) & 1ULL)) continue;
            if(p + L > plen) continue;
            if(memcmp(py + p, input + segstart[seg], (size_t)L) != 0) continue;
            if(full) {
                char syl[8];
                if(L > 6) continue;
                memcpy(syl, py + p, (size_t)L); syl[L] = '\0';
                if(pinyin_is_syllable(obj, syl)) next |= 1ULL << (p + L);
            }
            else {
                /* 末段前缀: 该音节可以比段更长 (xue'xi 对 学校 xue|xiao), 所有长度都要试 */
                for(int L2 = L; L2 <= 6 && p + L2 <= plen; L2++) {
                    char syl[8];
                    memcpy(syl, py + p, (size_t)L2); syl[L2] = '\0';
                    if(pinyin_is_syllable(obj, syl)) next |= 1ULL << (p + L2);
                }
            }
        }
        if(next == 0) return false;
        reach = next;
    }

    /* 上面的位掩码只是「快速必要条件」(把整桶扫描里绝大多数词挡掉, 不必查整词音节切分);
       通过后再做精确校验: 整词按字数切成 nchar 个真实音节, 输入段必须落在这些音节边界上
       (避免 xian' 误配 遐呢[xia|ne] 这类歧义)。 */
    int syl_lens[8];
    if(!pinyin_segment_word_syllables(obj, py, nchar, syl_lens)) return false;
    int pos = 0, syl_i = 0, syl_start = 0;
    for(int seg = 0; seg < nseg; seg++) {
        if(syl_i >= nchar) return false;
        int L = seglen[seg];
        bool last = (seg == nseg - 1);
        int rem = syl_lens[syl_i] - (pos - syl_start);
        if(last && !trailing_sep) {
            if(L > rem) return false;      /* 末段前缀须落在当前音节内 */
        }
        else {
            if(L != rem) return false;      /* 完整段须恰好填满当前音节 */
        }
        if(pos + L > plen) return false;
        if(memcmp(py + pos, input + segstart[seg], (size_t)L) != 0) return false;
        pos += L;
        if(pos - syl_start >= syl_lens[syl_i]) { syl_i++; syl_start = pos; }
    }
    return true;
}

/* 是否存在匹配带分隔输入的整词 (用于分隔符校验 + 自动拦截) */
static bool pinyin_segmented_has_prefix(lv_obj_t * obj, const char * input)
{
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;
    if(pinyin_ime->phrase_dict == NULL || *input == '\0') return false;
    uint8_t offset = input[0] - 'a';
    if(offset >= 26) return false;
    uint16_t count = pinyin_ime->phrase_num[offset];
    const lv_phrase_dict_t * p = &pinyin_ime->phrase_dict[pinyin_ime->phrase_pos[offset]];
    while(count--) {
        if(pinyin_segmented_match(obj, p->py, p->word, input)) return true;
        p++;
    }
    return false;
}

/* 重建候选 btnmatrix: 一次性列出全部候选(无分页/箭头), 每个候选按字数等比给宽.
   候选总宽超过容器时靠 cand_panel 横向滚动查看. */
static void pinyin_build_cand_map(lv_obj_t * obj)
{
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    lv_btnmatrix_ctrl_t ctrl_map[LV_PHRASE_CAND_MAX];
    uint16_t total_units = 0;
    uint16_t n = 0;

    /* 整句 DP 候选固定占第一位, 普通逐词/单字候选整体后移一位 */
    if(pinyin_ime->has_sentence) {
        pinyin_ime->sel_map[n] = pinyin_ime->sentence;
        uint8_t unit = (uint8_t)(pinyin_ime->sentence_chars + 1);
        if(unit < 2) unit = 2;
        if(unit > 15) unit = 15;
        ctrl_map[n] = (lv_btnmatrix_ctrl_t)unit;
        total_units += unit;
        n++;
    }

    for(uint16_t i = 0; i < (uint16_t)(pinyin_ime->cand_num - pinyin_ime->has_sentence); i++) {
        pinyin_ime->sel_map[n] = pinyin_ime->cand_word[i];

        /* 汉字是 3 字节 UTF-8; 单位宽 = 字数 + 1 (单字也 2 单位保证可点) */
        uint16_t wlen = (uint16_t)strlen(pinyin_ime->cand_word[i]) / 3;
        uint8_t unit = (uint8_t)(wlen + 1);
        if(unit < 2) unit = 2;
        if(unit > 15) unit = 15;
        ctrl_map[n] = (lv_btnmatrix_ctrl_t)unit;
        total_units += unit;
        n++;
    }
    pinyin_ime->sel_map[pinyin_ime->cand_num] = "";

    /* 先 set_map 分配 button_areas/ctrl_bits, 再设宽 + ctrl_map(等比宽) */
    lv_btnmatrix_set_map(pinyin_ime->cand_btnm, pinyin_ime->sel_map);
    lv_obj_set_width(pinyin_ime->cand_btnm, total_units * LV_IME_PINYIN_CAND_UNIT_PX);
    lv_btnmatrix_set_ctrl_map(pinyin_ime->cand_btnm, ctrl_map);

    /* 每次输入回卷到最左, 从第一个候选开始看 */
    lv_obj_scroll_to_x(pinyin_ime->cand_panel, 0, LV_ANIM_OFF);
}

static void pinyin_ime_clear_data(lv_obj_t * obj)
{
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;


#if LV_IME_PINYIN_USE_K9_MODE
    if(pinyin_ime->mode == LV_IME_PINYIN_MODE_K9) {
        pinyin_ime->k9_input_str_len = 0;
        pinyin_ime->k9_py_ll_pos = 0;
        pinyin_ime->k9_legal_py_count = 0;
        lv_memset_00(pinyin_ime->k9_input_str,  LV_IME_PINYIN_K9_MAX_INPUT);
        lv_memset_00(lv_pinyin_k9_cand_str, sizeof(lv_pinyin_k9_cand_str));
        strcpy(lv_pinyin_k9_cand_str[LV_IME_PINYIN_K9_CAND_TEXT_NUM], LV_SYMBOL_RIGHT"\0");
        strcpy(lv_pinyin_k9_cand_str[LV_IME_PINYIN_K9_CAND_TEXT_NUM + 1], "\0");
    }
#endif

    pinyin_ime->ta_count = 0;
    pinyin_ime->cand_num = 0;
    pinyin_ime->has_sentence = 0;
    pinyin_ime->manual_sep = 0;
    pinyin_ime->sentence_chars = 0;
    pinyin_ime->sentence[0] = '\0';
    lv_memset_00(pinyin_ime->input_char, sizeof(pinyin_ime->input_char));

    lv_obj_add_flag(pinyin_ime->cand_panel, LV_OBJ_FLAG_HIDDEN);
}


void lv_ime_pinyin_clear_all(lv_obj_t * obj)
{
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;
    if(pinyin_ime->ta) lv_textarea_set_text(pinyin_ime->ta, "");
    pinyin_ime_clear_data(obj);
}


void lv_ime_pinyin_reset_input(lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    pinyin_ime_clear_data(obj);
}


#if LV_IME_PINYIN_USE_K9_MODE
static void pinyin_k9_init_data(lv_obj_t * obj)
{
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    uint16_t py_str_i = 0;
    uint16_t btnm_i = 0;
    for(btnm_i = 19; btnm_i < (LV_IME_PINYIN_K9_CAND_TEXT_NUM + 21); btnm_i++) {
        if(py_str_i == LV_IME_PINYIN_K9_CAND_TEXT_NUM) {
            strcpy(lv_pinyin_k9_cand_str[py_str_i], LV_SYMBOL_RIGHT"\0");
        }
        else if(py_str_i == LV_IME_PINYIN_K9_CAND_TEXT_NUM + 1) {
            strcpy(lv_pinyin_k9_cand_str[py_str_i], "\0");
        }
        else {
            strcpy(lv_pinyin_k9_cand_str[py_str_i], " \0");
        }

        lv_btnm_def_pinyin_k9_map[btnm_i] = lv_pinyin_k9_cand_str[py_str_i];
        py_str_i++;
    }

    default_kb_ctrl_k9_map[0]  = LV_KEYBOARD_CTRL_BTN_FLAGS | 1;
    default_kb_ctrl_k9_map[4]  = LV_KEYBOARD_CTRL_BTN_FLAGS | 1;
    default_kb_ctrl_k9_map[5]  = LV_KEYBOARD_CTRL_BTN_FLAGS | 1;
    default_kb_ctrl_k9_map[9]  = LV_KEYBOARD_CTRL_BTN_FLAGS | 1;
    default_kb_ctrl_k9_map[10] = LV_KEYBOARD_CTRL_BTN_FLAGS | 1;
    default_kb_ctrl_k9_map[14] = LV_KEYBOARD_CTRL_BTN_FLAGS | 1;
    default_kb_ctrl_k9_map[15] = LV_KEYBOARD_CTRL_BTN_FLAGS | 1;
    default_kb_ctrl_k9_map[LV_IME_PINYIN_K9_CAND_TEXT_NUM + 16] = LV_KEYBOARD_CTRL_BTN_FLAGS | 1;
}

static void pinyin_k9_get_legal_py(lv_obj_t * obj, char * k9_input, const char * py9_map[])
{
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    uint16_t len = strlen(k9_input);

    if((len == 0) || (len >= LV_IME_PINYIN_K9_MAX_INPUT)) {
        return;
    }

    char py_comp[LV_IME_PINYIN_K9_MAX_INPUT] = {0};
    int mark[LV_IME_PINYIN_K9_MAX_INPUT] = {0};
    int index = 0;
    int flag = 0;
    int count = 0;

    uint32_t ll_len = 0;
    ime_pinyin_k9_py_str_t * ll_index = NULL;

    ll_len = _lv_ll_get_len(&pinyin_ime->k9_legal_py_ll);
    ll_index = _lv_ll_get_head(&pinyin_ime->k9_legal_py_ll);

    while(index != -1) {
        if(index == len) {
            if(pinyin_k9_is_valid_py(obj, py_comp)) {
                if((count >= ll_len) || (ll_len == 0)) {
                    ll_index = _lv_ll_ins_tail(&pinyin_ime->k9_legal_py_ll);
                    strcpy(ll_index->py_str, py_comp);
                }
                else if((count < ll_len)) {
                    strcpy(ll_index->py_str, py_comp);
                    ll_index = _lv_ll_get_next(&pinyin_ime->k9_legal_py_ll, ll_index);
                }
                count++;
            }
            index--;
        }
        else {
            flag = mark[index];
            if(flag < strlen(py9_map[k9_input[index] - '2'])) {
                py_comp[index] = py9_map[k9_input[index] - '2'][flag];
                mark[index] = mark[index] + 1;
                index++;
            }
            else {
                mark[index] = 0;
                index--;
            }
        }
    }

    if(count > 0) {
        pinyin_ime->k9_legal_py_count = count;
    }
}


/*true: visible; false: not visible*/
static bool pinyin_k9_is_valid_py(lv_obj_t * obj, char * py_str)
{
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    lv_pinyin_dict_t * cpHZ = NULL;
    uint8_t index = 0, len = 0, offset = 0;
    uint16_t ret = 1;
    volatile uint8_t count = 0;

    if(*py_str == '\0')    return false;
    if(*py_str == 'i')     return false;
    if(*py_str == 'u')     return false;
    if(*py_str == 'v')     return false;

    offset = py_str[0] - 'a';
    len = strlen(py_str);

    cpHZ  = &pinyin_ime->dict[pinyin_ime->py_pos[offset]];
    count = pinyin_ime->py_num[offset];

    while(count--) {
        for(index = 0; index < len; index++) {
            if(*(py_str + index) != *((cpHZ->py) + index)) {
                break;
            }
        }

        // perfect match
        if(len == 1 || index == len) {
            return true;
        }
        cpHZ++;
    }
    return false;
}


static void pinyin_k9_fill_cand(lv_obj_t * obj)
{
    static uint16_t len = 0;
    uint16_t index = 0, tmp_len = 0;
    ime_pinyin_k9_py_str_t * ll_index = NULL;

    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    tmp_len = pinyin_ime->k9_legal_py_count;

    if(tmp_len != len) {
        lv_memset_00(lv_pinyin_k9_cand_str, sizeof(lv_pinyin_k9_cand_str));
        strcpy(lv_pinyin_k9_cand_str[LV_IME_PINYIN_K9_CAND_TEXT_NUM], LV_SYMBOL_RIGHT"\0");
        strcpy(lv_pinyin_k9_cand_str[LV_IME_PINYIN_K9_CAND_TEXT_NUM + 1], "\0");
        len = tmp_len;
    }

    ll_index = _lv_ll_get_head(&pinyin_ime->k9_legal_py_ll);
    strcpy(pinyin_ime->input_char, ll_index->py_str);
    while(ll_index) {
        if((index >= LV_IME_PINYIN_K9_CAND_TEXT_NUM) || \
           (index >= pinyin_ime->k9_legal_py_count))
            break;

        strcpy(lv_pinyin_k9_cand_str[index], ll_index->py_str);
        ll_index = _lv_ll_get_next(&pinyin_ime->k9_legal_py_ll, ll_index); /*Find the next list*/
        index++;
    }
    pinyin_ime->k9_py_ll_pos = index;

    lv_obj_t * ta = pinyin_ime->ta;
    for(index = 0; index < pinyin_ime->k9_input_str_len; index++) {
        lv_textarea_del_char(ta);
    }
    pinyin_ime->k9_input_str_len = strlen(pinyin_ime->input_char);
    lv_textarea_add_text(ta, pinyin_ime->input_char);
}


static void pinyin_k9_cand_page_proc(lv_obj_t * obj, uint16_t dir)
{
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    lv_obj_t * ta = pinyin_ime->ta;
    uint16_t ll_len =  _lv_ll_get_len(&pinyin_ime->k9_legal_py_ll);

    if((ll_len > LV_IME_PINYIN_K9_CAND_TEXT_NUM) && (pinyin_ime->k9_legal_py_count > LV_IME_PINYIN_K9_CAND_TEXT_NUM)) {
        ime_pinyin_k9_py_str_t * ll_index = NULL;
        uint16_t tmp_btn_str_len = 0;
        int count = 0;

        ll_index = _lv_ll_get_head(&pinyin_ime->k9_legal_py_ll);
        while(ll_index) {
            if(count >= pinyin_ime->k9_py_ll_pos)   break;

            ll_index = _lv_ll_get_next(&pinyin_ime->k9_legal_py_ll, ll_index); /*Find the next list*/
            count++;
        }

        if((NULL == ll_index) && (dir == 1))   return;

        lv_memset_00(lv_pinyin_k9_cand_str, sizeof(lv_pinyin_k9_cand_str));
        strcpy(lv_pinyin_k9_cand_str[LV_IME_PINYIN_K9_CAND_TEXT_NUM], LV_SYMBOL_RIGHT"\0");
        strcpy(lv_pinyin_k9_cand_str[LV_IME_PINYIN_K9_CAND_TEXT_NUM + 1], "\0");

        // next page
        if(dir == 1) {
            count = 0;
            while(ll_index) {
                if(count >= (LV_IME_PINYIN_K9_CAND_TEXT_NUM - 1))
                    break;

                strcpy(lv_pinyin_k9_cand_str[count], ll_index->py_str);
                ll_index = _lv_ll_get_next(&pinyin_ime->k9_legal_py_ll, ll_index); /*Find the next list*/
                count++;
            }
            pinyin_ime->k9_py_ll_pos += count - 1;

        }
        // previous page
        else {
            count = LV_IME_PINYIN_K9_CAND_TEXT_NUM - 1;
            ll_index = _lv_ll_get_prev(&pinyin_ime->k9_legal_py_ll, ll_index);
            while(ll_index) {
                if(count < 0)  break;

                strcpy(lv_pinyin_k9_cand_str[count], ll_index->py_str);
                ll_index = _lv_ll_get_prev(&pinyin_ime->k9_legal_py_ll, ll_index); /*Find the previous list*/
                count--;
            }

            if(pinyin_ime->k9_py_ll_pos > LV_IME_PINYIN_K9_CAND_TEXT_NUM)
                pinyin_ime->k9_py_ll_pos -= 1;
        }

        lv_textarea_set_cursor_pos(ta, LV_TEXTAREA_CURSOR_LAST);
    }
}

#endif  /*LV_IME_PINYIN_USE_K9_MODE*/

#endif  /*LV_USE_IME_PINYIN*/



