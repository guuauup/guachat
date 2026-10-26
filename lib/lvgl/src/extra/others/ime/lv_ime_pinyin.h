/**
 * @file lv_ime_pinyin.h
 *
 */
#ifndef LV_IME_PINYIN_H
#define LV_IME_PINYIN_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "../../../lvgl.h"

#if LV_USE_IME_PINYIN != 0

#include "pinyin_dp.h"   /* 整句拼音 DP */

/*********************
 *      DEFINES
 *********************/
#define LV_IME_PINYIN_K9_MAX_INPUT  7

/* 26键整词候选缓冲: 展开后候选(整词+单字)最多条数 / 每条最大字节数 */
#define LV_PHRASE_CAND_MAX       64
#define LV_PHRASE_WORD_MAX_BYTES 16
#define LV_PHRASE_MATCH_MAX      2048 /* 整词排序前收集的匹配上限(含用户频次自学习); 词库4.8万后分隔符+高产音节可能>512 */

/**********************
 *      TYPEDEFS
 **********************/

typedef enum {
    LV_IME_PINYIN_MODE_K26,
    LV_IME_PINYIN_MODE_K9,
} lv_ime_pinyin_mode_t;

/*Data of pinyin_dict*/
typedef struct {
    const char * const py;
    const char * const py_mb;
} lv_pinyin_dict_t;

/*Data of phrase dict (26-key full pinyin word-level input, 整词输入)*/
typedef struct {
    const char * const py;
    const char * const word;
} lv_phrase_dict_t;

/*Data of 9-key input(k9) mode*/
typedef struct {
    char py_str[7];
} ime_pinyin_k9_py_str_t;

/*Data of lv_ime_pinyin*/
typedef struct {
    lv_obj_t obj;
    lv_obj_t * kb;
    lv_obj_t * ta;              /* Textarea (set separately when using custom btnmatrix) */
    lv_obj_t * cand_panel;      /* 候选栏容器(横向滚动): 内含加宽的候选 btnmatrix, 无翻页箭头 */
    lv_obj_t * cand_btnm;       /* 候选汉字 btnmatrix (宽度=全部候选, 超出容器左右拖动查看) */
    lv_pinyin_dict_t * dict;
    const lv_phrase_dict_t * phrase_dict; /* 整词词库 (NULL = 未启用) */
    lv_ll_t k9_legal_py_ll;
    char   cand_word[LV_PHRASE_CAND_MAX][LV_PHRASE_WORD_MAX_BYTES];         /* 展开后的候选词缓冲(整词+单字) */
    const char * sel_map[LV_PHRASE_CAND_MAX + 1];                            /* 候选栏 btnmatrix map (直接指向 cand_word[i]) */
    uint16_t cand_idx[LV_PHRASE_MATCH_MAX];   /* 整词排序缓冲: 匹配词的 phrase_dict 绝对下标 */
    uint16_t cand_score[LV_PHRASE_MATCH_MAX]; /* 整词排序缓冲: 对应用户频次 */
    uint8_t  cand_consume[LV_PHRASE_CAND_MAX]; /* 候选点击后消耗的输入字母数 (0=整串上屏; 前缀词 >0) */
    char   input_char[64];      /* Input box character (整句 DP 最长 31 字母 + 自动音节分隔 ' ) */
    char   sentence[PINYIN_DP_MAX_BYTES]; /* 整句 DP 候选 (候选栏第一项) */
    uint8_t sentence_chars;     /* 整句汉字数 (候选按钮宽度用) */
#if LV_IME_PINYIN_USE_K9_MODE
    char   k9_input_str[LV_IME_PINYIN_K9_MAX_INPUT]; /* 9-key input(k9) mode input string */
    uint16_t k9_py_ll_pos;      /* Current pinyin map pages(k9) */
    uint16_t k9_legal_py_count; /* Count of legal Pinyin numbers(k9) */
    uint16_t k9_input_str_len;  /* 9-key input(k9) mode input string max len */
#endif
    uint16_t ta_count;          /* The number of characters entered in the text box this time */
    uint16_t cand_num;          /* Number of candidates */
    uint16_t py_num[26];        /* Number and length of Pinyin */
    uint16_t py_pos[26];        /* Pinyin position */
    uint16_t phrase_num[26];    /* 整词词库每个首字母的词条数 */
    uint16_t phrase_pos[26];    /* 整词词库每个首字母的起始位置 */
    uint8_t  mode : 1;          /* Set mode, 1: 26-key input(k26), 0: 9-key input(k9). Default: 1. */
    uint8_t  k26_pinyin : 1;   /* K26 pinyin mode: 1 = pinyin(CN), 0 = English(EN). Default: 0. */
    uint8_t  has_sentence : 1;  /* 候选栏第一项是否为整句 DP 结果 */
    uint8_t  manual_sep : 1;    /* 用户按过分隔键 (input_char 里才有手动 '; 自动音节分隔不算) */
    uint8_t  sentence_enable : 1; /* 是否启用整句 DP (默认关，应用需要时打开) */
    uint16_t (*word_score_cb)(const char * word);  /* 整词用户频次回调 (候选排序用, NULL=0) */
    void     (*word_note_cb)(const char * word);   /* 选整词回调 (自学习记频, NULL=无) */
} lv_ime_pinyin_t;

/***********************
 * GLOBAL VARIABLES
 ***********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/
lv_obj_t * lv_ime_pinyin_create(lv_obj_t * parent);

/*=====================
 * Setter functions
 *====================*/

/**
 * Set the keyboard of Pinyin input method.
 * @param obj  pointer to a Pinyin input method object
 * @param dict pointer to a Pinyin input method keyboard
 */
void lv_ime_pinyin_set_keyboard(lv_obj_t * obj, lv_obj_t * kb);

/**
 * Use a custom btnmatrix as the keyboard for Pinyin input method.
 * This bypasses the lv_keyboard class, allowing custom button layouts.
 * @param obj   pointer to a Pinyin input method object
 * @param btnm  pointer to a btnmatrix object to use as keyboard
 * @param ta    pointer to the Text Area to input characters into
 */
void lv_ime_pinyin_set_kb_btnmatrix(lv_obj_t * obj, lv_obj_t * btnm, lv_obj_t * ta);

/**
 * Set the dictionary of Pinyin input method.
 * @param obj  pointer to a Pinyin input method object
 * @param dict pointer to a Pinyin input method dictionary
 */
void lv_ime_pinyin_set_dict(lv_obj_t * obj, lv_pinyin_dict_t * dict);

/**
 * Set the phrase dictionary of Pinyin input method (26-key full pinyin word-level input).
 * @param obj         pointer to a Pinyin input method object
 * @param phrase_dict pointer to a phrase dictionary (lv_phrase_dict_t[]), NULL to disable
 */
void lv_ime_pinyin_set_phrase_dict(lv_obj_t * obj, const lv_phrase_dict_t * phrase_dict);

/**
 * Set whole-word user-frequency callbacks (26-key 整词自学习).
 * word_score: 返回该词的用户频次(候选排序用, 越高越靠前), 未收录返回 0。
 * word_note:  用户点选某个整词时回调(记频)。
 * 传 NULL 关闭对应回调(缺省即关闭)。
 * @param obj  pointer to a Pinyin input method object
 */
void lv_ime_pinyin_set_word_score_cb(lv_obj_t * obj, uint16_t (*cb)(const char * word));
void lv_ime_pinyin_set_word_note_cb(lv_obj_t * obj, void (*cb)(const char * word));

/**
 * Enable/disable sentence-level DP candidate (整句 Viterbi) in 26-key mode.
 * @param obj  pointer to a Pinyin input method object
 * @param en   true: show the best whole-sentence path as the first candidate
 */
void lv_ime_pinyin_set_sentence_enable(lv_obj_t * obj, bool en);

/**
 * Set mode, 26-key input(k26) or 9-key input(k9).
 * @param obj  pointer to a Pinyin input method object
 * @param mode   the mode from 'lv_ime_pinyin_mode_t'
 */
void lv_ime_pinyin_set_mode(lv_obj_t * obj, lv_ime_pinyin_mode_t mode);


/*=====================
 * Getter functions
 *====================*/

/**
 * Set the dictionary of Pinyin input method.
 * @param obj  pointer to a Pinyin IME object
 * @return     pointer to the Pinyin IME keyboard
 */
lv_obj_t * lv_ime_pinyin_get_kb(lv_obj_t * obj);


/**
 * Set the dictionary of Pinyin input method.
 * @param obj  pointer to a Pinyin input method object
 * @return     pointer to the Pinyin input method candidate panel
 */
lv_obj_t * lv_ime_pinyin_get_cand_panel(lv_obj_t * obj);


/**
 * Set the dictionary of Pinyin input method.
 * @param obj  pointer to a Pinyin input method object
 * @return     pointer to the Pinyin input method dictionary
 */
lv_pinyin_dict_t * lv_ime_pinyin_get_dict(lv_obj_t * obj);

/*=====================
 * Other functions
 *====================*/

/**
 * Clear all Pinyin input state (input_char, ta_count, candidate panel hidden)
 * and empty the bound textarea. Used by long-press backspace to delete all
 * input content at once.
 * @param obj  pointer to a Pinyin input method object
 */
void lv_ime_pinyin_clear_all(lv_obj_t * obj);

/**
 * Clear only the Pinyin input state (input_char / candidates / sentence), leaving the
 * textarea text untouched. Call after the textarea is cleared externally (message sent,
 * target rebound) so stale input can't reject or mis-match the next keystrokes.
 * @param obj  pointer to a Pinyin input method object
 */
void lv_ime_pinyin_reset_input(lv_obj_t * obj);

/**********************
 *      MACROS
 **********************/

#endif  /*LV_IME_PINYIN*/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_USE_IME_PINYIN*/


