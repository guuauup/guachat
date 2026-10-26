# Skills

本目录是给编码 Agent 的技能包。每个 Skill 是一个带 frontmatter 的
`SKILL.md` + 中文版 `SKILL.zh_CN.md`，描述**触发条件**和工作流。

> 下面 5 个 `guachat-*` 技能是开发前必需项。Agent 必须先确认当前环境能
> 发现并读取它们；缺失时自行安装，安装失败要报告阻塞点，不能凭“目录存在”
> 宣称已加载。详见 [`AGENTS.md`](../AGENTS.md#11-必需技能开发前检查)。

| Skill | 用途 |
| --- | --- |
| [`guachat-develop/`](guachat-develop/) | 需求 → 分支 → 实现 → 验证 → 报告 |
| [`guachat-setup/`](guachat-setup/) | 安装 PlatformIO / 准备环境 / 首次构建 |
| [`guachat-build/`](guachat-build/) | 构建、内存预算、0x0 合并固件 |
| [`guachat-device-test/`](guachat-device-test/) | 确认后烧录、串口采集、设备清单 |
| [`guachat-debug/`](guachat-debug/) | 启动循环、花屏、I2C/SD、射频/音频排障 |

## 安装

Claude Code 从项目内 `.claude/skills` 加载技能：

```bash
python tools/install_skills.py
```

其他 Agent 如果不认这个目录，可以直接把整个目录复制到它支持的位置：

```bash
python tools/install_skills.py --target ~/.claude/skills
python tools/install_skills.py --print        # 只看路径
```

`--force` 会覆盖已存在的同名技能。安装后需要重启或重新加载 Agent。

## 写法约定

- frontmatter 的 `name` 与目录名一致。
- `description` 写清“什么时候用”，不要写功能宣传。
- 正文只写工作流、命令、硬性规则和文档回退路径。
- 中英文并列，英文供 Agent 检索，中文供人阅读。
