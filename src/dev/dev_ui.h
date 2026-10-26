/**
 * dev_ui.h — guachat_dev 的硬件自检界面
 *
 * 这是 BSP 用法示例，不是产品 UI。用户/Agent 应整体替换本文件。
 */
#ifndef DEV_UI_H
#define DEV_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/** 创建界面与 1 Hz 刷新定时器（需在 lv_init/display/input 之后调用） */
void dev_ui_build(void);

#ifdef __cplusplus
}
#endif

#endif /* DEV_UI_H */
