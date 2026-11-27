/* 3Danmu:非官方视频客户端,运行在自制系统的 3DS 上
 *
 * 操作:
 *   十字键上下   选择视频
 *   A           播放(播放中 A 暂停 / B 退出)
 *   Y           搜索
 *   X           管理选中视频的稍后再看
 *   L / R       上一页 / 下一页
 *   SELECT      回到热门
 *   START       退出程序
 */
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui.h"
#include "net.h"
#include "bili.h"
#include "player.h"
#include "thread_util.h"
#include "danmaku.h"
#include "ime.h"
#include "settings.h"
#include "app_flow.h"
#include "browse_ui.h"
#include "social_ui.h"
#include "pgc_ui.h"
#include "video_info.h"
#include "video_actions_ui.h"
#include "favorite_queue.h"
#include "thumb.h"
#include "subtitle.h"
#include "tls.h"
#include "vendor/qrcodegen.h"

#define MAX_LIST 20

/* 加大主线程栈(默认 32KB,ffmpeg 解码 + 深调用链不够用) */
unsigned int __stacksize__ = 256 * 1024;

/* 【线性堆必须显式声明,且预算里要算上字体】
 * 线性堆(linearAlloc)装的是所有要给硬件看的缓冲。完整清单(实测):
 *   字体图集       8.2MB  ← C2D_FontLoad 把字形表当纹理放这里!
 *                          (曾经漏算它,14MB 的线性堆被字体吃掉大半,
 *                           MVD 连 2.4MB 输入缓冲都分不出来,硬解全灭)
 *   MVD 工作区     5~7MB
 *   MVD 输入/输出  2.4MB
 *   视频纹理+vout  2.0MB
 *   音频/封面      0.5MB
 *   合计峰值      ~20MB
 * CIA 的默认划分远小于此;.3dsx 由 Homebrew Launcher 决定(其 env 参数
 * 优先于本变量,所以这里改大不影响 3dsx)。
 * 25MB 是为了 480P 硬解:L3.1 工作区约 8.5MB,加上字体 8.2 和其余缓冲,
 * 22MB 差一口气。普通堆拿剩下的(约 64-13-25=26MB),硬解路径下 ffmpeg
 * 只做解封装,足够;软解路径帧缓冲也在堆里,实测同样够用。 */
u32 __ctru_linear_heap_size = 25 * 1024 * 1024;

typedef enum { MODE_POPULAR, MODE_RECOMMEND, MODE_SEARCH,
               MODE_HISTORY, MODE_FAV, MODE_WATCHLATER, MODE_USER_VIDEOS } ListMode;

static BiliVideo s_list[MAX_LIST];
/* 【为什么要多一份 10KB 的暂存】接口层一进来就把 *count 清零、再逐条填,
 * 而那时旧的一页还画在屏幕上。直接写 s_list 的话,翻页途中 s_count 会短暂
 * 归零 —— 上屏于是露出居中的「加载中」,下屏跳出「加载失败?按 A 重试」,
 * 都不是真的。拉到暂存里、成功了才整体换上,翻页就只是某一帧换成新的一页。
 * 20 条 x ~0.5KB,买得起;顺带让 B 取消变成真的无损。 */
static BiliVideo s_stage[MAX_LIST];
static int s_stage_n;
static uint32_t s_request_generation;
static int64_t s_fav_id;
static char s_fav_title[128];
static BiliProfile s_user;
static int64_t s_list_account;
static BiliPageInfo s_fav_page_info;
static BiliApiResult s_fav_result;
static bool s_list_failed;
static BiliVideo s_action_video;
static bool choose_favorite_folder(void);
static void video_actions(void);
static void playback_notice(const char *message);
static void search_browse(void);
static void show_personal(void);
static void home_watchlater(void);
static bool show_up(const BiliProfile *profile);
static void play_video(BiliVideo video,bool favorite_source);
static void play_pgc(PgcSeries series,PgcEpisode episode);
static void process_pending_navigation(void);
static SocialSelection s_pending_navigation;
static SocialRelation s_user_relation;
static VideoInfo s_action_info;
static bool s_collection_requested,s_playing_favorite;

static int  s_count = 0;
static int  s_sel = 0;
static float s_scroll = 0.0f;       /* 列表滚动偏移(显示值,像素) */
static float s_scroll_t = 0.0f;     /* 滚动目标值:输入写这里,显示值每帧缓动跟随 */
#define ROW_H   66.0f               /* 图文卡片行高 */
#define LIST_Y  26.0f               /* 列表区顶部(顶栏下方) */
#define LIST_H  (240.0f - LIST_Y)
static int  s_page = 1;
/* 已知的最后一页(0 = 还不知道)。热门只有 22 页左右、收藏夹按实际条数,
 * 而接口不会提前告诉你 —— 只能翻到空页那一下才知道。记住它,是为了
 * 之后再按 R 不必再发一次注定为空的请求。换频道/换关键词要清零。 */
static int  s_page_end = 0;
static ListMode s_mode = MODE_RECOMMEND;   /* 默认进推荐(和官方 App 一致) */
static int s_pending_mode = -1;   /* 登录界面里点了频道:退出后切过去 */
static int s_hl_mode = -1;        /* 高亮覆盖:点击后立刻亮新的(-1=跟随 s_mode) */
static char s_keyword[128] = {0};
static char s_status[192] = "";
/* 名字以数字开头,所以宏名不能叫 3DANMU_VERSION(C 标识符不许数字打头) */
#define APP_VERSION "1.3.10-kina-navigation"

/* ---------- 分 P ----------
 * 200 是务实的上限:课程/合集偶尔上百 P,再多就不该在掌机上翻了。
 * 放文件作用域而不是栈上——一条 BiliPage 约 112 字节,200 条 22KB,
 * 而这条调用链底下还压着整个播放器。 */
#define MAX_PAGES 200
static BiliPage s_pages[MAX_PAGES];
static int s_npages = 0;
/* 给播放器的选集列表。播放器只读不存,所以这两个数组必须活得比播放久 ——
 * 放文件作用域,别改成栈上的临时变量。 */
static char  s_pg_label[MAX_PAGES][72];
static const char *s_pg_labelp[MAX_PAGES];
static int   s_pg_dur[MAX_PAGES];

static bool g_autoplay = false;
static bool g_danmaku = true;       /* 设置:弹幕开关 */
/* 清晰度。B 站的 qn 编号:16=360P 32=480P。
 * 【别再加 240P(qn=6)】试过,撤了:**绝大多数视频根本不提供这一档**,
 * 接口不会报错,而是默默返回 360P —— 设置页显示「240P」而实际是 360P,
 * 用户只会觉得「选了没用」。一个大部分时候什么都不做的选项,
 * 比没有这个选项更糟。相关的判断留在 log_quality 里(bili.c),
 * 哪天 B 站放开了能一眼看出来。 */
static int  g_qn = 16;
#define QN_360 16
#define QN_480 32
static const char *qn_name(int qn) { return qn == QN_480 ? "480P" : "360P"; }
static int qn_next(int qn) { return qn == QN_480 ? QN_360 : QN_480; }
static bool g_force_sw = false;     /* 设置:强制软解 */
static bool s_in_settings = false;  /* 当前在设置页 */
static bool s_debug_ui = false;

/* 下屏 console 只支持 ASCII:log 打英文,中文状态只画在上屏 */
static char s_busy[128] = "";  /* 状态条:正在做什么 / 上次失败原因(空=空闲) */

static void set_status(const char *ui_utf8, const char *log_ascii) {
	snprintf(s_status, sizeof(s_status), "%s", ui_utf8);
	if (log_ascii) printf("%s\n", log_ascii);
}

static void busy_frame(const char *msg);   /* 定义在下面 */
static int  run_bg(int (*fn)(void *), void *arg, const char *msg);  /* 同上 */
static bool s_job_cancelled;   /* 上一次 run_bg 是否被 B 掐掉,定义在下面 */

/* 列表加载最多试几次(含第一次)。3 次、退避 0.8s+1.6s —— 再多就该让
 * 用户自己决定了:接口真的挂了的话,机器替他干等只是把失败拖得更久。 */
#define LIST_RETRY_MAX 3

/* 后台作业的参数。这几个调用都在主线程上串行发起,同一时刻只有一个
 * 在跑,所以用文件作用域的变量传参最省事 —— 不必为每种作业各建一个结构。 */
static const char *s_job_bvid;
static int64_t s_job_cid;
static char *s_job_url;
static int s_job_qn;

static int pagelist_job(void *u) {
	(void)u;
	return bili_pagelist(s_job_bvid, s_pages, MAX_PAGES, &s_npages);
}
static int playurl_job(void *u) {
	(void)u;
	return bili_get_play_url(s_job_bvid, s_job_cid, s_job_qn, s_job_url, 2048);
}

/* 列表拉取的实体。放到后台线程上跑,主线程照常画帧 —— 直接在主线程调
 * 会冻住整整一秒(实测接口 0.8~1.1s),那一秒和死机分不出来。 */
static int list_fetch_job(void *unused) {
	(void)unused;
	switch (s_mode) {
	case MODE_SEARCH:  return bili_search(s_keyword, s_page, s_stage, MAX_LIST, &s_stage_n);
	case MODE_RECOMMEND: return bili_recommend(s_page, s_stage, MAX_LIST, &s_stage_n);
	case MODE_HISTORY: return bili_history(s_page, s_stage, MAX_LIST, &s_stage_n);
	case MODE_WATCHLATER:
        s_fav_result=bili_watchlater(s_page,s_stage,MAX_LIST,&s_fav_page_info);
        s_stage_n=s_fav_page_info.count;
        return s_fav_result.outcome==BILI_API_SUCCESS?0:-1;
    case MODE_USER_VIDEOS:
        s_fav_result=bili_user_videos(s_user.mid,s_page,s_stage,MAX_LIST,&s_fav_page_info);
        s_stage_n=s_fav_page_info.count;
        return s_fav_result.outcome==BILI_API_SUCCESS?0:-1;
    case MODE_FAV:
        s_fav_result = bili_fav_folder(s_fav_id, s_page, s_stage, MAX_LIST, &s_fav_page_info);
        s_stage_n = s_fav_page_info.count;
        return s_fav_result.outcome == BILI_API_SUCCESS ? 0 : -1;
	default:           return bili_popular(s_page, s_stage, MAX_LIST, &s_stage_n);
	}
}

static bool personal_list(void) {
    return s_mode==MODE_HISTORY || s_mode==MODE_FAV || s_mode==MODE_WATCHLATER;
}
static bool server_paged(void) {
    return s_mode==MODE_FAV || s_mode==MODE_WATCHLATER || s_mode==MODE_USER_VIDEOS;
}
static int64_t list_source_id(void) {
    return s_mode==MODE_USER_VIDEOS?s_user.mid:(s_mode==MODE_FAV?s_fav_id:0);
}
static void load_list(void) {
    AppRequestKey request = { ++s_request_generation, list_source_id(), s_mode, s_page };
	/* 【末页信息收在这一处判定】换频道、换关键词、重新搜索……每条路径
	 * 都会把 s_page 归 1,所以"回到第一页"就是"换了一批内容"的充要条件。
	 * 分散到那 9 个赋值点各写一句,必然漏 —— 这一版就当场漏了两个
	 * (ZL/ZR 切频道那两行是单行 if,和其他写法不一样)。
	 * 同一条规矩写在多处、漏在某处,今天已经在线程回收上栽过一次了。 */
	if (s_page == 1) s_page_end = 0;
	set_status("加载中...", "loading...");
	/* 【翻页时别清屏】先画一帧:**列表原样留着**,只在下屏状态条上提示。
	 * 原来这里把整个上屏清成一句「加载中…」,翻一页闪一次白 ——
	 * 而列表内容其实还在,清掉它只是让人以为东西没了。
	 * 首次加载是例外(还没有列表可留),那时 draw_list 会居中显示 s_status。 */
	const char *waiting = (s_count > 0) ? "加载新一页中" : "加载中";
	busy_frame(waiting);

	/* 【顺序要紧】先停封面,再发接口请求。
	 * 反过来的话,换页时上一页的封面线程会和新的 API 请求并发 ——
	 * 而「图片请求不与 API 请求同时在飞」正是下面放开图片并发的前提。
	 * 这个不变式靠调用顺序保证,比靠一把全局大锁便宜得多。
	 * 放在画帧**之后**:这样那一帧里封面还在,不会先闪掉一排图。 */
	thumb_stop();

	/* 【自动重试】列表接口偶发失败是常态:3DS 的 httpc 本就脆,
	 * B 站对掌机 UA 也时不时风控性地拒一次。这种失败重来一次多半就好了,
	 * 让用户自己按 A 只是把一件机器该做的事推给他。
	 * 但重试必须**可中断**:等待期间照常画帧、B 键随时放弃,
	 * 否则网络真断了就成了一个按不动的三秒卡顿。 */
	int r = -1;
	for (int attempt = 0; ; attempt++) {
		/* 翻页时不提示:列表原样留着,几百毫秒后换成新的一页就是了。
		 * 首次加载没有列表可留,那时才需要一句「加载中」。 */
		r = run_bg(list_fetch_job, NULL, waiting);
		if (r == 0 || attempt >= LIST_RETRY_MAX - 1) break;
        if (server_paged() && s_fav_result.outcome == BILI_API_REJECTED) break;
		if (s_job_cancelled) break;   /* 用户主动取消:不是错误,别重试 */
		if (net_is_shutting_down() || aptShouldClose()) break;

		/* 退避 0.8s / 1.6s:两次之间不留间隔的话,失败往往只是重复一遍 */
		bool give_up = false;
		char m[96];
		snprintf(m, sizeof(m), "加载失败,重试中 %d/%d(B 放弃)",
		         attempt + 2, LIST_RETRY_MAX);
		u64 t0 = osGetTime();
		u32 wait_ms = 800u * (u32)(attempt + 1);
		while (osGetTime() - t0 < wait_ms) {
			/* 这是我们自己的子循环,一帧扫一次输入 —— 和登录页同一套规矩 */
			hidScanInput();
			if (hidKeysDown() & KEY_B) { give_up = true; break; }
			if (net_is_shutting_down() || aptShouldClose()) { give_up = true; break; }
			busy_frame(m);
		}
		if (give_up) break;
	}

    AppRequestKey current = { s_request_generation, list_source_id(), s_mode, s_page };
    if (s_job_cancelled || !app_request_matches(request, current)) {
        s_count = 0;
        s_list_failed = true;
        set_status("已取消加载，A重试", "load result discarded");
        snprintf(s_busy, sizeof(s_busy), "已取消加载，A重试");
        return;
    }
    s_list_failed = r != 0;
    /* Successful empty pages are authoritative, including a newly selected folder. */
    if (r == 0 && app_list_at_end(server_paged(),
        s_fav_page_info.has_more, s_stage_n)) s_page_end = s_page;
	if (r == 0) {
		memcpy(s_list, s_stage, sizeof(s_list));
		s_count = s_stage_n;
        s_list_account=personal_list()?bili_account_id():0;
	}
	if (r == 0 || !s_job_cancelled) {
		/* 取消时连滚动位置一起留着 —— 那才叫"当无事发生" */
		s_sel = 0;
		s_scroll = 0.0f;
		s_scroll_t = 0.0f;
	}
	if (r != 0 && s_job_cancelled) {
		/* 取消了就当无事发生:保留原来的列表,状态条清干净 */
		set_status("", "load cancelled");
		s_busy[0] = 0;
	} else if (r != 0) {
		s_count = 0;
		char sbuf[160];
		snprintf(sbuf, sizeof(sbuf), "加载失败 %s(A 重试 / B 返回)",
		         bili_last_error());
		set_status(sbuf, "load failed, A=retry B=back");
		snprintf(s_busy, sizeof(s_busy), "加载失败:%s", bili_last_error());
	} else {
		char buf[64];
		snprintf(buf, sizeof(buf), "loaded page %d, %d items", s_page, s_count);
		set_status(s_count ? "" : (server_paged() && s_fav_page_info.has_more ? "本页无可用视频，R下一页" : "这里还没有视频"), buf);
		s_busy[0] = 0;      /* 加载成功:清掉上次的失败原因 */
	}
	if (s_count > 0)
		thumb_start(s_list, s_count);
}

static void fmt_meta(const BiliVideo *v, char *out, size_t n) {
	char views[32] = "";
	if (v->views >= 100000000)
		snprintf(views, sizeof(views), " %.1f亿", v->views / 1e8);
	else if (v->views >= 10000)
		snprintf(views, sizeof(views), " %.1f万", v->views / 1e4);
	else if (v->views >= 0)
		snprintf(views, sizeof(views), " %ld", (long)v->views);
	char dur[24] = "";
	if (v->duration > 0)
		snprintf(dur, sizeof(dur), "  %d:%02d", v->duration / 60, v->duration % 60);
	snprintf(out, n, "%s%s播放%s", v->author, views, dur);
}

static void draw_list(void) {
	ui_begin();
	/* 封面上传预算:静止时每帧 1 张;滚动进行中为 0——同步 GPU 传输
	 * 哪怕 1-2ms 也会在滚动时被肉眼捕捉为顿挫,滚完再补 */
	{
		float d = s_scroll - s_scroll_t;
		thumb_new_frame((d > -1.0f && d < 1.0f) ? 1 : 0);
	}
	/* 层级:citro2d 深度测试为 GEQUAL —— z 大者在上,同 z 后画者胜。
	 * 实测数据:选中高亮 z=0.4 时会盖掉缩略图(z=0.2),行文字(z=0.5)
	 * 会穿透后画的顶栏(z=0.4)。所以:高亮 0.15 < 图 0.2 < 文字 0.5
	 * < 顶栏底 0.6 < 顶栏字 0.7 */

	/* 列表:图文卡片,s_scroll 像素级滚动(左摇杆) */
	{
		int first = (int)(s_scroll / ROW_H);
		if (first < 0) first = 0;
		for (int i = first; i < s_count; i++) {
			/* 坐标取整:小数位置会让文字/边缘在像素间跳,滚动看着发抖 */
			float y = (float)(int)(LIST_Y + i * ROW_H - s_scroll);
			if (y > 240) break;
			if (i == s_sel)
				ui_rect_z(0, y, 0.15f, 400, ROW_H - 4, UI_COL_SEL);
			const C2D_Image *im = thumb_get(i);
			if (im) {
				C2D_DrawImageAt(*im, 6, y + 2, 0.2f, NULL, 1.0f, 1.0f);
			} else {
				ui_rect_z(6, y + 2, 0.18f, THUMB_W, THUMB_H,
				          C2D_Color32(0x30, 0x30, 0x3C, 0xFF));
				ui_text(6 + 38, y + 24, 0.45f, UI_COL_DIM, "…");
			}
			if (i == s_sel) {
				/* 选中行:标题过长时缓慢跑马灯(停 1.2s → 滚动 →
				 * 到尾停 1s → 回起点)。裁剪住行框,不糊到封面/顶栏 */
				static float marq = 0;
				static int marq_sel = -1;
				static u64 marq_t0 = 0;
				static float marq_tw = 0;   /* 标题宽度:换行才重算 */
				static size_t marq_len = 0; /* 换页后同下标不同视频也要重算 */
				size_t tlen = strlen(s_list[i].title);
				if (marq_sel != i || marq_len != tlen) {
					marq_sel = i;
					marq_len = tlen;
					marq = 0;
					marq_t0 = osGetTime();
					/* 量宽是完整的字形解析,别每帧都做 */
					marq_tw = ui_text_width(s_list[i].title, UI_SHARP);
				}
				float tw = marq_tw;
				const float mw = 286.0f;
				if (tw > mw) {
					float span = tw - mw + 12.0f;
					if (osGetTime() - marq_t0 > 1200) {
						marq += 0.55f;
						if (marq > span + 55.0f) {   /* 尾部停顿后回起点 */
							marq = 0;
							marq_t0 = osGetTime();
						}
					}
					float off = marq > span ? span : marq;
					ui_clip(110, y + 2, mw, 24);
					ui_text(110 - off, y + 2, UI_SHARP, UI_COL_WHITE,
					        s_list[i].title);
					ui_unclip();
				} else {
					ui_text(110, y + 2, UI_SHARP, UI_COL_WHITE,
					        s_list[i].title);
				}
			} else {
				ui_text_clipped(110, y + 2, UI_SHARP, UI_COL_TEXT,
				                s_list[i].title, 286);
			}
			char meta[128];
			fmt_meta(&s_list[i], meta, sizeof(meta));
			ui_text_clipped(110, y + 42, UI_SHARP, UI_COL_DIM, meta, 286);
		}
	}
	if (s_count == 0)
		ui_text(120, 110, UI_SHARP, UI_COL_DIM, s_status);

	/* 顶栏:z 抬到行文字(0.5)之上才能真正遮住上浮的行 */
	ui_rect_z(0, 0, 0.6f, 400, 26, UI_COL_ACCENT);
	char title[160];
	switch (s_mode) {
	case MODE_SEARCH:
		snprintf(title, sizeof(title), "搜索: %s  P%d", s_keyword, s_page);
		break;
	case MODE_RECOMMEND:
		snprintf(title, sizeof(title), "为你推荐");
		break;
	case MODE_HISTORY:
		snprintf(title, sizeof(title), "历史记录  P%d", s_page);
		break;
	case MODE_WATCHLATER:
        snprintf(title,sizeof(title),"稍后再看  P%d",s_page);
        break;
    case MODE_USER_VIDEOS:
        snprintf(title,sizeof(title),"%s 的投稿 P%d",s_user.name,s_page);
        break;
    case MODE_FAV:
		snprintf(title, sizeof(title), "%s  P%d", s_fav_title[0] ? s_fav_title : "收藏夹", s_page);
		break;
	default:
		snprintf(title, sizeof(title), "热门  P%d", s_page);
		break;
	}
	ui_text_clipped_z(6, 3, 0.7f, UI_SHARP, UI_COL_WHITE, title, 300);
	const char *user = bili_username();
	ui_text_clipped_z(302, 3, 0.7f, UI_SHARP, UI_COL_WHITE,
	                  user ? user : "未登录", 95);
}

typedef struct {
	bool login, settings, hist, fav, popular, recommend, personal, watch_add;
    bool up_profile,up_follow,play_all;
} ListActions;

static void draw_bottom_list(bool touched, float tx, float ty, ListActions *act) {
	ui_begin_bottom();
	ui_text(10, 4, UI_SHARP, UI_COL_TEXT, bili_logged_in() ? "已登录" : "未登录");
	if (s_count == 0)
		ui_text(108, 4, UI_SHARP, UI_COL_ACCENT, s_list_failed ? "加载失败，A重试" : "暂无视频");
	{	/* 底部状态条:在做什么 + 总进度 */
		int done = 0, total = 0;
		bool busy = thumb_progress(&done, &total);
		ui_rect(10, 206, 300, 28, C2D_Color32(0x26, 0x26, 0x30, 0xFF));
		if (s_busy[0]) {  /* 当前动作 或 上次失败原因 */
			ui_text_clipped_z(18, 210, 0.55f, UI_SHARP, UI_COL_WHITE,
			                  s_busy, 284);
		} else if (busy && total > 0) {
			/* 只写文字,不涂进度条:封面是逐张浮现的,列表本身就是进度,
			 * 底下再来一条爬动的色块只是抢注意力 */
			char pbuf[48];
			snprintf(pbuf, sizeof(pbuf), "加载封面 %d/%d", done, total);
			ui_text_z(18, 210, 0.55f, UI_SHARP, UI_COL_WHITE, pbuf);
		} else {
			/* 空闲时兼作按键说明(别再单画一行,会和条重叠——踩过) */
			ui_text_z(18, 210, 0.55f, UI_SHARP, UI_COL_DIM,
			          s_count ? "A播放 X稍后再看 Y搜索 L/R翻页"
			                  : "等待加载(A 重试)");
		}
	}
    if(s_mode==MODE_USER_VIDEOS) {
        ui_text_clipped(10,30,UI_SHARP,UI_COL_ACCENT,s_user.name,300);
        ui_text_clipped(10,57,UI_SHARP,UI_COL_DIM,s_user.sign,300);
        if(ui_button(10,88,300,34,"UP主资料",UI_COL_SEL,touched,tx,ty))act->up_profile=true;
        const char *relation=s_user_relation.known?(s_user_relation.following?"已关注":"关注"):"关注 / 查询状态";
        if(s_user.mid!=bili_account_id() && ui_button(10,129,300,38,relation,s_user_relation.following?UI_COL_ACCENT:UI_COL_SEL,touched,tx,ty))act->up_follow=true;
        if(ui_button(10,173,300,28,"返回推荐",UI_COL_SEL,touched,tx,ty))act->recommend=true;
        return;
    }
    if(s_mode==MODE_FAV) {
        ui_text_clipped(10,32,UI_SHARP,UI_COL_TEXT,s_fav_title,300);
        if(ui_button(10,70,300,40,"播放全部",UI_COL_ACCENT,touched,tx,ty))act->play_all=true;
        if(ui_button(10,124,145,40,"切换收藏夹",UI_COL_SEL,touched,tx,ty))act->fav=true;
        if(ui_button(165,124,145,40,"返回推荐",UI_COL_SEL,touched,tx,ty))act->recommend=true;
        if(ui_button(10,173,300,28,"个人中心",UI_COL_SEL,touched,tx,ty))act->personal=true;
        return;
    }
    int hl=(s_hl_mode>=0)?s_hl_mode:(int)s_mode;
    if(ui_button(10,32,145,40,"推荐",hl==MODE_RECOMMEND?UI_COL_ACCENT:UI_COL_SEL,touched,tx,ty))act->recommend=true;
    if(ui_button(165,32,145,40,"热门",hl==MODE_POPULAR?UI_COL_ACCENT:UI_COL_SEL,touched,tx,ty))act->popular=true;
    if(ui_button(10,80,145,40,"个人中心",UI_COL_SEL,touched,tx,ty))act->personal=true;
    if(ui_button(165,80,145,40,"收藏",hl==MODE_FAV?UI_COL_ACCENT:UI_COL_SEL,touched,tx,ty))act->fav=true;
    if(ui_button(10,128,145,40,"设置",UI_COL_SEL,touched,tx,ty))act->settings=true;
    if(ui_button(165,128,145,40,bili_logged_in()?"注销":"扫码登录",UI_COL_SEL,touched,tx,ty))act->login=true;

}

/* ---------- 设置页 ----------
 * 下屏是可滚动列表,上屏是「你刚碰的那一项」的说明。
 * 双屏机器上「下屏操作、上屏解释」是最自然的分工 —— 像「解码方式」
 * 「画面比例」这类一句话说不清的选项,以前只能靠用户点了看效果去猜。
 *
 * 说明文字要**预先折好行**:上屏可用宽度 384px,汉字在清晰档约 18px 一个,
 * 一行放得下 20 个左右。自动折行要按字宽逐字量,而这些文字是写死的,
 * 写的时候折一次比每帧算一次划算。 */
enum { SET_AUTOPLAY, SET_DANMAKU, SET_QN, SET_DECODE, SET_CACHE, SET_DEBUG, SET_BACK, SET_N };

static void settings_rows(UiRow *r, char *cachebuf, size_t cbn) {
    g_danmaku = settings_get("danmaku", 1) != 0;
    r[SET_AUTOPLAY] = (UiRow){ "打开视频自动播放", g_autoplay ? "开" : "关",
        "关闭后先显示视频信息。\n按播放才开始取流。\n选其他P仍保持等待，\n此选项会自动记住。" };
	if (thumb_cache_clearing())
		/* 带上百分比:光一句「清理中…」分不出是在干活还是卡住了,
		 * 而缓存里几千个文件时它本来就要转好一会儿 */
		snprintf(cachebuf, cbn, "清理中 %d%%", thumb_cache_clear_pct());
	else
		snprintf(cachebuf, cbn, "清理 %dMB", (int)(thumb_cache_kb() / 1024));
	r[SET_DANMAKU] = (UiRow){ "弹幕", g_danmaku ? "开" : "关",
		"是否加载并显示弹幕。\n关掉可以省下加载时间和\n绘制开销。" };
	r[SET_QN]      = (UiRow){ "清晰度", qn_name(g_qn),
		"画面的分辨率。\n480P 更清晰,需要登录,\n也更吃解码性能。\n"
		"取流失败会自动回落 360P。" };
	r[SET_DECODE]  = (UiRow){ "解码方式", g_force_sw ? "强制软解" : "自动",
		"自动 = 有硬件解码器就用它,\n失败自动退回软件解码。\n"
		"软解 = 一律用 CPU 解码,\n兼容性最好,但慢很多。\n\n"
		"通常保持「自动」即可。" };
	r[SET_CACHE]   = (UiRow){ "封面缓存", cachebuf,
		"列表封面存在 SD 卡上,\n下次遇到同一个视频就不用\n重新下载。\n"
		"点一下清空;超过 100MB\n也会自动清。" };
	r[SET_DEBUG]   = (UiRow){ "调试台", NULL,
		"切到全屏日志页。\n记录最近 400 行:网络请求、\n解码状态、错误码。\n"
		"出问题时把它拍下来,\n比任何文字描述都有用。\n手指拖动滚动,双击退出。" };
	r[SET_BACK]    = (UiRow){ "返回", NULL, "回到视频列表。\nB 键同样可以。" };
}

static void draw_settings(bool touched, bool holding, bool released,
                          float tx, float ty) {
	UiRow rows[SET_N];
	char cachebuf[32];
	settings_rows(rows, cachebuf, sizeof(cachebuf));

	/* 上屏:说明。没选中任何一项时给一段总览,别留空屏 */
	int f = ui_list_focus();
	const char *ttl = (f >= 0 && f < SET_N) ? rows[f].name : "设置";
	const char *body = (f >= 0 && f < SET_N) ? rows[f].help
	                 : "下屏点按修改。\n碰哪一项,这里就说明哪一项。";
	char brand[96];
	snprintf(brand, sizeof(brand), "3Danmu v%s   原版作者小红书 94133173379",
	         APP_VERSION);
	ui_help_draw(ttl, body, "仅供学习交流  严禁用于商业用途", brand);

	if (ui_console_active()) return;   /* 调试台占着下屏 */
	int hit = ui_list_draw("设置", rows, SET_N, touched, holding, released, tx, ty);
	switch (hit) {
    case SET_AUTOPLAY:
        g_autoplay = !g_autoplay;
        settings_set("autoplay", g_autoplay);
        break;
    case SET_DANMAKU:
		g_danmaku = !g_danmaku;
		settings_set("danmaku", g_danmaku);
		break;
	case SET_QN:
		g_qn = qn_next(g_qn);
		settings_set("qn", g_qn);
		break;
	case SET_DECODE:
		g_force_sw = !g_force_sw;
		settings_set("force_sw", g_force_sw);
		break;
	case SET_CACHE:
		/* 【异步】目录里可能有几万个文件,同步删会把界面冻住 */
		thumb_cache_clear_async();
		break;
	case SET_DEBUG:
		/* 帧结束后再切:当帧内切会被 GPU 覆盖成花屏 */
		s_debug_ui = true;
		break;
	case SET_BACK:
		s_in_settings = false;
		break;
	default: break;
	}
}

static void do_search(void) { search_browse(); }

static void do_login(void) {
	if (bili_logged_in()) {
		bili_logout();
        ++s_request_generation;
        s_fav_id = 0; s_fav_title[0] = 0;
        if (s_mode == MODE_HISTORY || s_mode == MODE_FAV || s_mode == MODE_WATCHLATER) {
            thumb_stop(); s_count = 0; s_stage_n = 0;
            s_mode = MODE_RECOMMEND; s_page = 1; s_page_end = 0;
            load_list();
        }
        set_status("已注销", "logged out");
		return;
	}

	char qurl[512], qkey[64];
	if (bili_qr_generate(qurl, sizeof(qurl), qkey, sizeof(qkey)) != 0) {
		set_status("获取二维码失败", "QR generate failed");
		return;
	}

	uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
	uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];
	if (!qrcodegen_encodeText(qurl, tmp, qr, qrcodegen_Ecc_MEDIUM,
	                          1, 10, qrcodegen_Mask_AUTO, true)) {
		set_status("二维码生成失败", "QR encode failed");
		return;
	}

	printf("scan QR to log in, B to cancel\n");
	s_pending_mode = -1;   /* 清掉上次残留,否则会污染本次结果 */
	u64 last_poll = 0;
	while (aptMainLoop()) {
		/* 一帧只能 scan 一次!scan 两次会把触摸的"按下沿"吃掉
		 * (第二次 kDown 已经变 0),表现为下屏按钮完全点不动 */
		hidScanInput();
		u32 kd = hidKeysDown();
		if (kd & KEY_B) { set_status("已取消登录", "login cancelled"); return; }
		touchPosition tp = { 0, 0 };
		bool touched = (kd & KEY_TOUCH) != 0;
		if (touched) hidTouchRead(&tp);

		ui_begin();
		ui_text(96, 8, UI_SHARP, UI_COL_TEXT, "手机 B 站 App 扫码登录");
		ui_qr(qr, 200, 128, 4);
		ui_text(104, 222, UI_SHARP, UI_COL_DIM, "B 取消 / 下屏可直接切换频道");
		/* 下屏就是平时那张主页面(不另做界面):点哪个频道就切哪个,
		 * 高亮跟着走,登录随之取消 */
		ListActions act = { 0 };
		if (!ui_console_active())
			draw_bottom_list(touched, tp.px, tp.py, &act);
		ui_end();
		if (act.popular || act.recommend || act.hist || act.fav) {
			/* 历史/收藏仍需登录,登录未完成时点它们无意义 → 只接受热门/推荐 */
			if (act.popular || act.recommend) {
				s_pending_mode = act.popular ? MODE_POPULAR : MODE_RECOMMEND;
				set_status("已取消登录", "login cancelled");
				return;
			}
		}
		if (act.settings) {   /* 设置在登录页点了也当取消 */
			set_status("已取消登录", "login cancelled");
			return;
		}

		u64 now = osGetTime();
		if (now - last_poll >= 2000) {
			last_poll = now;
			int code = -1;
			if (bili_qr_poll(qkey, &code) == 0) {
				if (code == 0) {
					/* code==0 只说明「扫码这一步过了」,不代表 cookie 拿到手了。
					 * 以前这里无条件报「登录成功」,于是出问题时屏幕说成功、
					 * 顶栏说未登录 —— 提示本身在骗人,白白多查了一轮 */
					if (bili_logged_in()) {
						set_status("登录成功", "login OK");
					} else {
						set_status("登录未生效,请重试", "login failed: cookies rejected");
						printf("qr: code=0 but nav says not logged in\n");
					}
					return;
				}
				if (code == 86038) {
					/* 过期,换一张 */
					if (bili_qr_generate(qurl, sizeof(qurl), qkey, sizeof(qkey)) != 0 ||
					    !qrcodegen_encodeText(qurl, tmp, qr, qrcodegen_Ecc_MEDIUM,
					                          1, 10, qrcodegen_Mask_AUTO, true)) {
						set_status("二维码刷新失败", "QR refresh failed");
						return;
					}
					printf("QR refreshed\n");
				} else if (code == 86090) {
					printf("scanned, confirm on phone\n");
				}
			}
		}
	}
}

/* 阻塞网络操作前调用:立即画一帧,把"正在做什么"刷到状态条上 */
/* 【换 P 时别退回列表页】置位后 busy_frame 只画一句提示,不画视频列表。
 * 在播放中换 P,视觉上应该是「同一部片子换一集」,而闪回一整屏列表
 * 会让人以为已经退出播放了 —— 何况一两秒后又跳回播放器,更乱。 */
static bool s_busy_minimal = false;

/* ---------- 把阻塞的网络调用挪出主线程 ----------
 * 取列表、查分P、解析播放地址都是同步 HTTP,实测每次 0.8~1.1 秒。
 * 直接在主线程上调,那一秒界面完全冻住 —— 连"正在做什么"的动画都不动,
 * 和死机没法区分(这正是被反复反馈的"卡住")。
 *
 * 做法不是把整套流程改成异步(那要重写调用链),而是**把这一次调用
 * 放到临时线程上,主线程原地转着画帧**。逻辑顺序完全不变,
 * 调用方拿到的还是同一个返回值,只是期间界面活着。 */
typedef struct {
	int (*fn)(void *);
	void *arg;
	volatile int done;
	int ret;
} AsyncJob;

static void async_job_thread(void *p) {
	AsyncJob *j = (AsyncJob *)p;
	j->ret = j->fn(j->arg);
	__dmb();
	j->done = 1;
}

/* 返回 fn 的返回值。建不出线程就退回同步调用 —— 慢一点也比不能用强。 */
/* 上一次 run_bg 是不是被用户按 B 掐掉的。调用方据此决定「失败」要不要
 * 报错、要不要重试 —— 主动取消不是错误,不该弹「加载失败」也不该自动重来。 */
static int run_bg_impl(int (*fn)(void *), void *arg, const char *msg, bool cancellable) {
	AsyncJob job = { fn, arg, 0, -1 };
    s_job_cancelled = false;
	/* 优先级比主线程低一档:它只是在等网络,不该和界面抢 */
	Thread th = NULL;
	static const int cores[] = { 2, 3, -2 };
	/* ARM -O2 stack-usage: favorite page + HTTP chain exceeds 20KB;
	 * redirects can retain about43KB. Keep64KB for this single joined worker. */
	for (int i = 0; i < 3 && !th; i++)
		th = threadCreate(async_job_thread, &job, 64 * 1024, 0x31, cores[i], false);
	if (!th) return fn(arg);
	static const char *dots[4] = { "", ".", "..", "..." };
	bool cancelling = false;
	s_job_cancelled = false;
	while (!job.done) {
        if (!aptMainLoop() || net_is_shutting_down() || aptShouldClose()) {
            net_shutdown_begin();
            svcSleepThread(10000000ULL);
            continue;
        }
		hidScanInput();
		/* 【B 取消】httpc 的请求既没有超时也不看标志位,唯一能中断它的
		 * 办法是从外面把连接掐掉 —— net_cancel_streams 就是干这个的
		 * (挂起和退出路径本来就在用它,是条验证过的路)。
		 * 掐完那个阻塞调用会很快带着错误返回,于是这个循环自然结束。 */
		if (cancellable && !cancelling && (hidKeysDown() & KEY_B)) {
			cancelling = true;
			s_job_cancelled = true;
			net_cancel_requests();
		}
        if (cancelling) net_cancel_requests();
		char m[96];
		/* 不换成「正在取消」:掐掉连接后阻塞调用几十毫秒内就带错误返回了,
		 * 为这一瞬间换一句话,看着反倒像又开始干别的活。只收起 B 的提示 */
		if (msg[0]) snprintf(m, sizeof(m), "%s%s%s",
		                     msg, dots[(osGetTime() / 300) % 4],
		                     (cancelling || !cancellable) ? "" : "   (B 取消)");
		else m[0] = 0;      /* 空消息 = 界面照常,状态条不占用 */
		busy_frame(m);
    }
    threadJoin(th, UINT64_MAX);
    threadFree(th);
	return job.done ? job.ret : -1;
}

/* Reads can discard a cancelled result. A scheduled account mutation must
 * finish with confirmed/rejected/unknown; B cannot promise it was never sent. */
static int run_bg(int (*fn)(void *), void *arg, const char *msg) {
    return run_bg_impl(fn,arg,msg,true);
}

static void busy_frame(const char *msg) {
	snprintf(s_busy, sizeof(s_busy), "%s", msg);
	if (s_busy_minimal) {
		/* 两屏都要画:少画一屏的话,GPU 的两个后台缓冲各留着一份旧内容,
		 * 表现就是闪烁(这个坑在选集页和调试台那里已经踩过) */
		ui_begin();
		float tw = ui_text_width(msg, UI_SHARP);
		ui_text(200.0f - tw / 2.0f, 112, UI_SHARP, UI_COL_TEXT, msg);
		if (!ui_console_active()) {
			ui_begin_bottom();
			ui_text_clipped(10, 110, UI_SHARP, UI_COL_DIM, msg, 300);
		}
		ui_end();
		return;
	}
	draw_list();
	if (!ui_console_active()) {
		ListActions dummy = { 0 };
		draw_bottom_list(false, 0, 0, &dummy);
	}
	ui_end();
}

/* 播放器发弹幕时的登录回调:未登录则拉起扫码流程 */
static bool login_cb(void) {
	if (!bili_logged_in())
		do_login();
	/* 播放器内登录时若在登录页点了频道,这里没人消费会留成陈旧状态,
	 * 下次点历史/收藏就会莫名跳到那个频道(高亮也就"错位")→ 直接丢弃 */
	s_pending_mode = -1;
	return bili_logged_in();
}

/* 分P 一行的显示文本:"P3  标题"(没标题就只有 "P3") */
static void page_label(const BiliPage *pg, char *out, size_t n) {
	if (pg->title[0]) snprintf(out, n, "P%d  %s", pg->page, pg->title);
	else              snprintf(out, n, "P%d", pg->page);
}

/* 【选集页已移进播放器】原来这里有一个 choose_page():开播前的独立一页。
 * 改掉的原因是它决定了上屏能显示什么 —— 跑到那一页时播放器已经退出、
 * 纹理已经释放,上屏没有画面可留,只能另画一套标题+时长的排版。
 * 而「换一集」这个动作本来就发生在看片当中,上屏理应停在暂停的那一帧。
 *
 * 现在它是播放器里的一个子页面(和评论区同构):上屏保持暂停,
 * 下屏整个换成列表。播放器仍然不碰分P 数据 —— 标签和时长从这里传进去,
 * 选中后只回一个下标。
 *
 * 教训:界面归属不该只看「代码放哪儿更整齐」,还要看**它需要什么上下文**。
 * 这一页需要的是「视频还在、只是停住了」,那它就只能待在播放器里。 */


static void player_busy_frame(const char *msg) {
    bool previous=s_busy_minimal;s_busy_minimal=true;
    busy_frame(msg);s_busy_minimal=previous;
}

/* Bounded folder list; overflow is explicit and never silently means default. */
#define FOLDER_CAP 128
static BiliFavFolder s_folders[FOLDER_CAP];
static BiliPageInfo s_folder_info;
static BiliApiResult s_folder_result;
static int folder_job(void *unused) {
    (void)unused;
    s_folder_result=bili_fav_folders(s_folders,FOLDER_CAP,&s_folder_info);
    return s_folder_result.outcome==BILI_API_SUCCESS ? 0 : -1;
}

static bool choose_favorite_folder(void) {
    thumb_stop();
    s_busy_minimal=true;
    int result=run_bg(folder_job,NULL,"加载收藏夹");
    s_busy_minimal=false;
    if (result!=0 || s_job_cancelled) {
        snprintf(s_busy,sizeof(s_busy),"%s",s_job_cancelled?"已取消":bili_last_error());
        if (s_count && !net_is_shutting_down()) thumb_start(s_list,s_count);
        return false;
    }
    int hit=browse_folders(s_folders,s_folder_info.count,s_folder_info.truncated);
    bool picked=hit>=0 && hit<s_folder_info.count;
    if(picked) {
        s_fav_id=s_folders[hit].id;
        snprintf(s_fav_title,sizeof(s_fav_title),"%s",s_folders[hit].title);
        ++s_request_generation;
    }
    if (!picked && s_count && !net_is_shutting_down()) thumb_start(s_list,s_count);
    return picked;
}

/* Browse modal runner shares the existing worker lifetime and cancellation.
 * A cancelled read cannot publish a late success into a page. */
static int browse_run(int (*fn)(void *),void *arg,const char *msg,bool cancellable) {
    bool previous=s_busy_minimal;s_busy_minimal=true;
    busy_frame(msg); /* Draw a modal immediately, including synchronous fallback. */
    int result=run_bg_impl(fn,arg,msg,cancellable);
    s_busy_minimal=previous;
    if(result==0 || s_job_cancelled)s_busy[0]=0;
    else snprintf(s_busy,sizeof(s_busy),"操作未完成：%.85s",bili_last_error());
    return (cancellable && s_job_cancelled)?-1:result;
}
static void restore_browse(void) {
    s_busy_minimal=false;
    /* A modal can expire account A and log into B before returning. Bind old
     * personal cards to their actual owner, not just a logged-in boolean. */
    if(personal_list() && (!bili_account_id() || s_list_account!=bili_account_id())) {
        s_count=s_stage_n=0;s_list_account=0;s_fav_id=0;s_fav_title[0]=0;
        memset(s_list,0,sizeof(s_list));memset(s_stage,0,sizeof(s_stage));
        memset(&s_fav_page_info,0,sizeof(s_fav_page_info));
        ++s_request_generation;s_mode=MODE_RECOMMEND;s_hl_mode=-1;
        s_page=1;s_page_end=0;s_list_failed=true;
        set_status("账号已变化，A加载推荐", "discard personal list after identity change");
        snprintf(s_busy,sizeof(s_busy),"账号已变化，请重新打开个人列表");
    }
    ui_list_reset();
    if(s_count && !net_is_shutting_down() && !aptShouldClose())thumb_start(s_list,s_count);
}
typedef struct { bool own;int64_t mid;BiliProfile profile;BiliApiResult result; } ProfileJob;
static int profile_job(void *arg) {
    ProfileJob *j=arg;
    j->result=j->own?bili_profile(&j->profile):bili_user_profile(j->mid,&j->profile);
    return j->result.outcome==BILI_API_SUCCESS?0:-1;
}
typedef struct { int64_t mid;SocialRelation relation;BiliApiResult result; } RelationJob;
static int relation_job(void *arg) {
    RelationJob *j=arg;j->result=social_relation(j->mid,&j->relation);
    return j->result.outcome==BILI_API_SUCCESS?0:-1;
}
static void enqueue_selection(SocialSelection selection) {
    if(selection.kind!=SOCIAL_NONE)s_pending_navigation=selection;
}
static bool show_up(const BiliProfile *profile) {
    ProfileJob job={.mid=profile->mid};
    thumb_stop();s_busy_minimal=true;
    int result=browse_run(profile_job,&job,"加载UP资料",true);
    if(s_job_cancelled || net_is_shutting_down() || aptShouldClose()) {restore_browse();return false;}
    BiliProfile shown=result==0?job.profile:*profile;
    RelationJob relationship={.mid=shown.mid};
    if(shown.mid!=bili_account_id())browse_run(relation_job,&relationship,"查询关注状态",true);
    while(!net_is_shutting_down() && !aptShouldClose()) {
        const char *label=shown.mid==bili_account_id()?"当前账号":relationship.relation.known?(relationship.relation.following?"已关注":"关注"):"关注 / 查询状态";
        BrowseChoice choice=browse_profile_relationship(&shown,false,result==0?"":"资料未更新，可尝试查看投稿",label);
        if(choice==BROWSE_SUBMISSIONS) {
            s_user=shown;s_user_relation=relationship.relation;s_mode=MODE_USER_VIDEOS;s_hl_mode=-1;s_page=1;s_count=0;
            ++s_request_generation;s_busy_minimal=false;load_list();return true;
        }
        if(choice==BROWSE_RELATION) {
            if(shown.mid==bili_account_id())continue;
            if(!bili_logged_in() && !login_cb())continue;
            relationship.relation=social_relationship_ui(shown.mid,shown.name,browse_run);continue;
        }
        break;
    }
    restore_browse();return false;
}
static void search_browse(void) {
    thumb_stop();s_busy_minimal=true;
    int kind=browse_search_kind();char input[128]={0};
    const char *prompt=kind==2?"搜索番剧":kind==1?"搜索UP主":"搜索视频";
    if(kind<0 || !ime_input(prompt,s_keyword,input,sizeof(input)) || !input[0]) {
        restore_browse();return;
    }
    if(kind==0) {
        snprintf(s_keyword,sizeof(s_keyword),"%s",input);
        s_mode=MODE_SEARCH;s_hl_mode=-1;s_page=1;s_count=0;
        s_busy_minimal=false;load_list();return;
    }
    if(kind==2) {
        PgcSeries series;PgcEpisode episode;
        if(pgc_browse_search(input,browse_run,&series,&episode))play_pgc(series,episode);
        restore_browse();return;
    }
    BiliProfile selected;
    while(!net_is_shutting_down() && !aptShouldClose()) {
        s_busy_minimal=true;thumb_stop();
        if(!browse_users(input,browse_run,&selected)) {
            snprintf(s_busy,sizeof(s_busy),"%s",s_job_cancelled?"已取消":bili_last_error());break;
        }
        if(show_up(&selected))return;
    }
    restore_browse();
}
static void show_personal(void) {
    thumb_stop();s_busy_minimal=true;
    if(!login_cb()) {restore_browse();return;}
    ProfileJob job={.own=true};
    if(browse_run(profile_job,&job,"加载个人中心",true)!=0) {
        snprintf(s_busy,sizeof(s_busy),"%s",s_job_cancelled?"已取消":bili_last_error());restore_browse();return;
    }
    int64_t owner=job.profile.mid;
    while(owner>0 && owner==bili_account_id() && !net_is_shutting_down() && !aptShouldClose()) {
        BrowseChoice choice=browse_profile(&job.profile,true,"个人中心");
        if(choice==BROWSE_BACK)break;
        ListMode want=MODE_RECOMMEND;
        if(choice==BROWSE_HISTORY)want=MODE_HISTORY;
        else if(choice==BROWSE_WATCHLATER)want=MODE_WATCHLATER;
        else if(choice==BROWSE_SUBMISSIONS){want=MODE_USER_VIDEOS;s_user=job.profile;memset(&s_user_relation,0,sizeof(s_user_relation));}
        if(want!=MODE_RECOMMEND) {
            s_mode=want;s_hl_mode=-1;s_page=1;s_count=0;s_busy_minimal=false;++s_request_generation;load_list();return;
        }
        SocialSelection selected={0};
        if(choice==BROWSE_FOLLOWING || choice==BROWSE_FANS)selected=social_people_ui(owner,choice==BROWSE_FANS,browse_run);
        if(owner!=bili_account_id())break;
        if(selected.kind!=SOCIAL_NONE){enqueue_selection(selected);break;}
    }
    restore_browse();
}
typedef struct { BiliVideo video; } WatchResolveJob;
static int watch_resolve_job(void *arg) {
    WatchResolveJob *j=arg;
    return bili_get_cid(j->video.bvid,&j->video.cid,&j->video.aid);
}
static void home_watchlater(void) {
    if(s_sel<0 || s_sel>=s_count) {
        snprintf(s_busy,sizeof(s_busy),"请先选中一个视频");return;
    }
    WatchResolveJob job={.video=s_list[s_sel]};
    thumb_stop();s_busy_minimal=true;
    if(job.video.aid<=0 && browse_run(watch_resolve_job,&job,"查询视频编号",true)!=0) {
        snprintf(s_busy,sizeof(s_busy),"%s",s_job_cancelled?"已取消":bili_last_error());
        restore_browse();return;
    }
    bool attempted=false;
    if(!net_is_shutting_down() && !aptShouldClose())
        attempted=browse_watchlater_add(&job.video,browse_run,login_cb);
    if(attempted && s_mode==MODE_WATCHLATER && s_list_account==bili_account_id() &&
       bili_account_id()>0 && !net_is_shutting_down() && !aptShouldClose()) {
        /* Never leave a removed card visible if the refresh fails/cancels. */
        s_busy_minimal=false;s_count=0;load_list();
        if(!s_list_failed && !s_count && s_page>1 && !s_job_cancelled &&
           !net_is_shutting_down() && !aptShouldClose()) {
            s_page=1;load_list();
        }
        return;
    }
    restore_browse();
}

static int collection_index(const VideoInfo *info,const BiliVideo *video) {
    if(!info || info->partial || info->truncated)return -1;
    for(int i=0;i<info->count;i++)
        if(info->episodes[i].aid==video->aid && info->episodes[i].cid==video->cid)return i;
    return -1;
}
static void video_up(void) {
    if(s_action_video.owner_mid<=0){playback_notice("UP主信息暂不可用，请返回后重新加载视频");return;}
    s_pending_navigation=(SocialSelection){.kind=SOCIAL_UP,.profile={.mid=s_action_video.owner_mid,.fans=-1,.videos=-1}};
    snprintf(s_pending_navigation.profile.name,sizeof(s_pending_navigation.profile.name),"%s",s_action_video.author);
    player_request_navigation();
}
static void video_actions(void) {
    ui_list_reset();int focus=0;VideoAccountState state;
    video_account_state(&s_action_video,browse_run,&state);
    while(aptMainLoop() && !net_is_shutting_down() && !aptShouldClose()) {
        hidScanInput();u32 down=hidKeysDown();if(down&KEY_B)break;
        if((down&KEY_LEFT) && focus%2)focus--;
        if((down&KEY_RIGHT) && !(focus%2) && focus<3)focus++;
        if((down&KEY_UP) && focus>=2)focus-=2;
        if((down&KEY_DOWN) && focus<2)focus+=2;
        touchPosition touch={0};if(down&KEY_TOUCH)hidTouchRead(&touch);
        ui_begin();ui_text_clipped(10,20,UI_SHARP,UI_COL_TEXT,s_action_video.title,380);
        ui_text_clipped(10,70,UI_SHARP,UI_COL_DIM,s_action_video.author,380);
        ui_text(10,112,UI_SHARP,UI_COL_DIM,"粉色表示已确认的互动状态");
        if(s_playing_favorite)ui_text(10,150,UI_SHARP,UI_COL_DIM,"当前收藏夹播放队列已启用");
        else if(s_action_info.partial || s_action_info.truncated)ui_text(10,150,UI_SHARP,UI_COL_DIM,"合集信息不完整，已禁用合集连播");
        ui_begin_bottom();int account=-1;char label[64];
        snprintf(label,sizeof label,"%s%s",focus==0?"> ":"",state.liked==BILI_LIKE_CONFIRMED?"已点赞":"点赞");
        if(ui_button(10,16,145,38,label,state.liked==BILI_LIKE_CONFIRMED?UI_COL_ACCENT:UI_COL_SEL,down&KEY_TOUCH,touch.px,touch.py))account=0;
        snprintf(label,sizeof label,"%s%s",focus==1?"> ":"",state.coins>0?"已投币":"投币");
        if(ui_button(165,16,145,38,label,state.coins>0?UI_COL_ACCENT:UI_COL_SEL,down&KEY_TOUCH,touch.px,touch.py))account=1;
        snprintf(label,sizeof label,"%s%s",focus==2?"> ":"",state.favorite?"已收藏":"收藏");
        if(ui_button(10,64,145,38,label,state.favorite?UI_COL_ACCENT:UI_COL_SEL,down&KEY_TOUCH,touch.px,touch.py))account=2;
        bool up=ui_button(165,64,145,38,focus==3?"> 查看UP主":"查看UP主",UI_COL_SEL,down&KEY_TOUCH,touch.px,touch.py);
        int index=collection_index(&s_action_info,&s_action_video);bool collection=false;
        if(!s_playing_favorite && index>=0 && s_action_info.count>1)
            collection=ui_button(10,112,300,38,"从当前P开始合集连播",UI_COL_SEL,down&KEY_TOUCH,touch.px,touch.py);
        bool back=ui_button(10,174,300,34,"返回视频",UI_COL_SEL,down&KEY_TOUCH,touch.px,touch.py);
        ui_text(10,218,UI_SHARP,UI_COL_DIM,"方向键选择 A打开 B返回");ui_end();
        if(back)break;
        if(down&KEY_A){if(focus<3)account=focus;else up=true;}
        if(account>=0){video_account_action_ui(&s_action_video,account,browse_run,login_cb);video_account_state(&s_action_video,browse_run,&state);}
        if(up){video_up();if(s_action_video.owner_mid>0)break;}
        if(collection){s_collection_requested=true;player_request_navigation();break;}
    }
    ui_list_reset();
}

/* This gate owns first Play: metadata and page selection never start a stream. */
static bool wait_for_play(const BiliVideo *video,int *page,AppPlaybackSession *session) {
    while (aptMainLoop() && !net_is_shutting_down()) {
        hidScanInput();
        u32 down=hidKeysDown();
        if (down&KEY_B) return false;
        touchPosition touch={0};
        if (down&KEY_TOUCH) hidTouchRead(&touch);
        ui_begin();
        thumb_new_frame(1);
        const C2D_Image *cover=s_sel>=0 && s_sel<s_count && !strcmp(s_list[s_sel].bvid,video->bvid)?thumb_get(s_sel):NULL;
        if (cover) C2D_DrawImageAt(*cover,104,12,0.2f,NULL,2.0f,2.0f);
        ui_text_clipped(10,140,UI_SHARP,UI_COL_TEXT,video->title,380);
        ui_text_clipped(10,170,UI_SHARP,UI_COL_DIM,s_pg_label[*page],380);
        ui_text(10,207,UI_SHARP,UI_COL_DIM,"按 A 或播放才开始，B 返回。");
        ui_begin_bottom();
        bool play=ui_button(10,20,300,44,"播放",UI_COL_ACCENT,down&KEY_TOUCH,touch.px,touch.py);
        bool prev=ui_button(10,80,145,44,"上一 P",UI_COL_SEL,down&KEY_TOUCH,touch.px,touch.py);
        bool next=ui_button(165,80,145,44,"下一 P",UI_COL_SEL,down&KEY_TOUCH,touch.px,touch.py);
        bool actions=ui_button(10,146,300,44,"视频互动",UI_COL_SEL,down&KEY_TOUCH,touch.px,touch.py);
        ui_text(10,218,UI_SHARP,UI_COL_DIM,"A播放 X互动 上下选P B返回");
        ui_end();
        int pick=*page;
        if ((prev || (down&KEY_UP)) && pick>0) --pick;
        if ((next || (down&KEY_DOWN)) && pick+1<s_npages) ++pick;
        if (pick!=*page)
            app_session_next(session,(PlayerResult){PLAYER_PAGE,pick,false,0,0},page,s_npages);
        if (actions || (down&KEY_X)) {
            s_action_video.cid=s_pages[*page].cid;
            video_actions();
            if(s_pending_navigation.kind!=SOCIAL_NONE || s_collection_requested)return false;
            continue; /* The modal consumes this input frame, including A+X. */
        }
        if (play || (down&KEY_A)) { app_session_begin(session); return true; }
    }
    return false;
}

/* 真正开播一条流。cid 由调用方给 —— 多 P 视频必须传**选中那一 P**的 cid,
 * 弹幕、字幕、进度上报全按 cid 走,只有它换对了才是真的换了一集。 */
static PlayerResult play_stream(BiliVideo *v, int64_t cid, const char *disp_title,
                               PlayerStartOptions options) {
    s_busy_minimal=true;
    PlayerResult result = { .reason=PLAYER_NETWORK_ERROR, .page_index=-1, .paused=options.paused, .error_code=-1 };
    g_qn=settings_get("qn",QN_360)==QN_480?QN_480:QN_360;
    g_danmaku = settings_get("danmaku", 1) != 0;
	set_status("解析播放地址...", "resolving play url...");
	busy_frame("解析播放地址...");
	ui_trace("resolving playurl qn=%d", g_qn);
	char url[2048];
	/* 弹幕先起跑:取流那几个 API 往返是等延迟、不吃带宽的,
	 * 这段时间正好给弹幕下载用,能省下一两秒 */
	if (g_danmaku)
		dm_ensure_async(cid);
	/* 字幕不在这里拉:此刻正与 取流/弹幕 并发,3DS httpc 在多路并发下
	 * 会把响应张冠李戴(实测:字幕内容是别的视频的)。改为播放真正开始、
	 * 其它请求都收摊之后,由播放器单独去拉(player.c 里 sub_kicked) */
	sub_free();   /* 先清干净,杜绝上一个视频的残留 */
	int used_qn = g_qn;
	s_job_bvid = v->bvid; s_job_cid = cid; s_job_url = url;
	s_job_qn = g_qn;
	int r = run_bg(playurl_job, NULL, "解析播放地址");
	/* 带上原因:光看 r=-1 分不出「没发请求」「请求失败」「接口拒绝」,
	 * 而这三者的修法完全不同 */
	ui_trace("playurl r=%d err=%s", r, bili_last_error());
	if (r != 0 && !s_job_cancelled && !net_is_shutting_down() && g_qn != QN_360) { /* 拿不到就回落 360P(最通用的一档) */
		printf("qn=%d failed, fallback to 360P\n", g_qn);
		s_job_qn = QN_360;
		r = run_bg(playurl_job, NULL, "解析播放地址(360P)");
		used_qn = 16;
	}
	if (s_job_cancelled) {
		/* 用户按 B 掐掉的:当作没点过这个视频,别弹错误 */
		set_status("", "playurl cancelled");
		s_busy[0] = 0;
		dm_free();
		sub_free();
		return (PlayerResult){ s_job_cancelled ? PLAYER_BACK : PLAYER_NETWORK_ERROR, -1, options.paused, r ,0};
	}
	if (r != 0) {
		{
			const char *why = bili_last_error();
			char msg[128];
			snprintf(msg, sizeof(msg), "取流失败:%s",
			         (why && why[0]) ? why : "可能需登录或视频受限");
			set_status(msg, "playurl failed");
			snprintf(s_busy, sizeof(s_busy), "%s", msg);
		}
		dm_free();   /* 弹幕已经起跑了,不播就得把线程收掉 */
		sub_free();
		return (PlayerResult){ s_job_cancelled ? PLAYER_BACK : PLAYER_NETWORK_ERROR, -1, options.paused, r ,0};
	}
	player_set_meta(v->aid, cid, v->bvid);
	/* 【每次开播都从存档重读弹幕开关】播放页里也能开关弹幕,而那个改动
	 * 落在 player.c 的静态变量上,main.c 的 g_danmaku 并不知道。
	 * 不重读的话,这里会拿一个过期的值把用户刚才的选择覆盖掉。
	 * 存档是两处共同的真相来源,以它为准。 */
	g_danmaku = settings_get("danmaku", g_danmaku ? 1 : 0) != 0;
	player_set_prefs(g_danmaku, g_force_sw, used_qn);
	s_busy[0] = 0;
	result = player_play_session(url, disp_title, options);
	/* 【回列表前把状态条擦掉】播放器开流时借 busy_frame 写过「连接中」
	 * 「载入视频」,那几个字一直留在 s_busy 里 —— 播放期间被视频盖着看
	 * 不见,退出来才露出来,于是列表底下挂着一句早就做完的事。
	 * 状态条写的是"正在做什么",没在做就该是空的。 */
	s_busy[0] = 0;
	ui_trace_sync("exit-path: dm_free");
	dm_free();
	ui_trace_sync("exit-path: sub_free");
	sub_free();
    return result;
}

typedef struct { BiliVideo seed;BiliApiResult result; } InfoJob;
static int info_job(void *arg) {
    InfoJob *j=arg;j->result=video_info_get(&j->seed,&s_action_info);
    return j->result.outcome==BILI_API_SUCCESS?0:-1;
}
typedef struct { FavoriteQueue state,current;BiliVideo next;FavoriteQueueResult result;bool open;int64_t aid; } QueueJob;
static QueueJob s_queue_job;
static bool s_favorite_all_ready;
static FqOrder s_favorite_all_order;
static int favorite_all_job(void *unused) {
    (void)unused;
    s_queue_job.result=fq_open_all(s_fav_id,s_favorite_all_order,(uint32_t)osGetTime(),&s_queue_job.state,&s_queue_job.next);
    return s_queue_job.result==FQ_NEXT?0:-1;
}
static int favorite_next_job(void *arg) {
    QueueJob *j=arg;
    if(j->open) {
        j->result=fq_open(s_fav_id,s_page,j->aid,&j->state);j->open=false;
        if(j->result!=FQ_NEXT)return -1;
    }
    j->current=j->state;
    j->result=fq_next(&j->state,&j->next);
    return j->result==FQ_NEXT?0:-1;
}
static int favorite_previous_job(void *unused) {
    (void)unused;s_queue_job.state=s_queue_job.current;
    s_queue_job.result=fq_previous(&s_queue_job.state,&s_queue_job.next);
    return s_queue_job.result==FQ_NEXT?0:-1;
}
static bool favorite_prefetch(void) {
    int r=browse_run(favorite_next_job,&s_queue_job,"准备收藏夹下一条",true);
    if(s_job_cancelled)s_queue_job.result=FQ_STOP;
    return r==0 && s_queue_job.result==FQ_NEXT;
}
static bool favorite_retryable(void) {
    return s_queue_job.result==FQ_ERROR && s_queue_job.state.issue==FQ_ISSUE_REQUEST &&
        !s_queue_job.state.last_result.login_required &&
        s_queue_job.current.current_aid>0 && s_queue_job.current.account_mid==bili_account_id();
}
static void favorite_queue_notice(void) {
    const FavoriteQueue *q=&s_queue_job.state;
    if(q->last_result.http_status==412 || q->last_result.http_status==429) {
        snprintf(s_busy,sizeof(s_busy),"收藏夹请求受限 HTTP %d，请等待60秒后重试",q->last_result.http_status);return;
    }
    const char *msg = s_queue_job.result==FQ_END ? "收藏夹没有更多可播放视频" :
                      s_queue_job.result==FQ_LIMIT ? "收藏夹超过随机播放上限1000条" :
                      q->issue==FQ_ISSUE_ACCOUNT ? "账号已变化，收藏夹连播已停止" :
                      q->last_result.login_required ? "登录已失效，请重新登录后播放收藏夹" :
                      q->issue==FQ_ISSUE_CHANGED ? "收藏夹内容已变化，请重新打开收藏夹" :
                      q->issue==FQ_ISSUE_FORMAT ? "收藏夹数据格式异常，连播已停止" :
                      s_queue_job.result==FQ_STOP ? "收藏夹连播已停止：内容或账号已变化" :
                      "收藏夹下一条加载失败，连播已停止";
    snprintf(s_busy,sizeof(s_busy),"%s",msg);
}
/* No pointer into the browsing list is retained by playback. A queue owns its
 * fixed source and every destination is consumed only after player teardown. */
static void play_video(BiliVideo video,bool favorite_source) {
    thumb_stop();video_info_init(&s_action_info);
    VideoInfo collection;video_info_init(&collection);
    bool explicit_cid=false,have_next=false,continue_started=false,continue_paused=false;
    int collection_pos=-1,invalid=0;
    s_collection_requested=false;s_playing_favorite=favorite_source;
    if(favorite_source) {
        if(s_favorite_all_ready) {
            s_favorite_all_ready=false;s_queue_job.open=false;continue_started=true;
        } else {
            memset(&s_queue_job,0,sizeof(s_queue_job));s_queue_job.open=true;s_queue_job.aid=video.aid;
        }
        have_next=favorite_prefetch();
        if(s_job_cancelled || s_queue_job.state.account_mid!=bili_account_id() ||
           s_queue_job.state.last_result.login_required || s_queue_job.state.issue==FQ_ISSUE_CHANGED ||
           (s_queue_job.result!=FQ_NEXT && s_queue_job.result!=FQ_END && s_queue_job.result!=FQ_ERROR)) {
            if(!s_job_cancelled) favorite_queue_notice();
            goto playback_done;
        }
        /* A failure to prepare a later item does not invalidate the selected
         * video. Play it under the same account, then report the queue error. */
    }
    while(!net_is_shutting_down() && !aptShouldClose()) {
        if(favorite_source && s_queue_job.state.account_mid!=bili_account_id()) {
            snprintf(s_busy,sizeof(s_busy),"账号已变化，收藏夹连播已停止");break;
        }
        InfoJob meta={.seed=video};
        int metadata=browse_run(info_job,&meta,"获取视频信息",true);
        if(s_job_cancelled)break;
        if(metadata==0) {
            int64_t selected_cid=video.cid;video=s_action_info.video;
            if(explicit_cid)video.cid=selected_cid;
            invalid=0;
        } else if(continue_started && meta.result.outcome==BILI_API_REJECTED &&
                  meta.result.has_code && (meta.result.code==-404 || meta.result.code==62002 || meta.result.code==62012)) {
            if(++invalid>5) {snprintf(s_busy,sizeof(s_busy),"连续失效视频超过5条，已停止连播");break;}
            if(favorite_source && have_next) {
                video=s_queue_job.next;have_next=favorite_prefetch();
                if(s_job_cancelled)break;
                continue;
            }
            if(collection_pos>=0 && collection_pos+1<collection.count) {
                video=collection.episodes[++collection_pos];explicit_cid=true;continue;
            }
            break;
        } else if(video.cid<=0 || (continue_started && metadata!=0)) {
            snprintf(s_busy,sizeof(s_busy),"获取信息失败：%s",bili_last_error());break;
        }
        if(net_is_shutting_down() || aptShouldClose())break;
        s_npages=0;
        if(!explicit_cid && video.pages!=1) {
            s_job_bvid=video.bvid;
            if(browse_run(pagelist_job,NULL,"检查分P",true)!=0) {
                if(s_job_cancelled || continue_started)break;
                s_npages=0;
            }
        }
        if(s_job_cancelled || net_is_shutting_down())break;
        if(!s_npages) {
            s_npages=1;s_pages[0]=(BiliPage){.cid=video.cid,.page=1,.duration=video.duration};
            snprintf(s_pages[0].title,sizeof(s_pages[0].title),"%.95s",video.title);
        }
        int cur=0;
        for(int i=0;i<s_npages;i++) {
            if(s_pages[i].cid==video.cid)cur=i;
            page_label(&s_pages[i],s_pg_label[i],sizeof(s_pg_label[i]));
            s_pg_labelp[i]=s_pg_label[i];s_pg_dur[i]=s_pages[i].duration;
        }
        AppPlaybackSession session;app_session_init(&session,continue_started || g_autoplay);
        if(continue_started)session.paused=continue_paused;
        s_action_video=video;
        PlayerResult terminal={PLAYER_BACK,-1,false,0,0};
        bool collection_next=collection_pos>=0 && collection_pos+1<collection.count;
        for(;;) {
            if(net_is_shutting_down() || aptShouldClose())break;
            if(!session.started && !wait_for_play(&video,&cur,&session))break;
            if(favorite_source && s_queue_job.state.account_mid!=bili_account_id()) {
                snprintf(s_busy,sizeof(s_busy),"账号已变化，收藏夹连播已停止");break;
            }
            s_action_video=video;s_action_video.cid=s_pages[cur].cid;
            char title[320];snprintf(title,sizeof(title),"P%d %s | %s",s_pages[cur].page,s_pages[cur].title,video.title);
            player_set_pages(s_pg_labelp,s_pg_dur,s_npages,cur);
            player_set_navigation(s_npages>1?PLAYER_NAV_PAGES:favorite_source||collection_pos>=0?PLAYER_NAV_ITEMS:PLAYER_NAV_PAGES,
                cur>0||(favorite_source&&fq_has_previous(&s_queue_job.current))||collection_pos>0,
                cur+1<s_npages||have_next||collection_next||(favorite_source&&favorite_retryable()));
            /* A failed queue continuation must reach the caller at EOF so it
             * can show the error instead of looking like a clean final item. */
            bool queue_failed=favorite_source && !have_next &&
                s_queue_job.result!=FQ_END && s_queue_job.result!=FQ_NEXT;
            PlayerStartOptions options={session.paused,cur+1<s_npages || have_next || collection_next || queue_failed,session.resume_seconds};
            terminal=play_stream(&video,s_pages[cur].cid,title,options);
            if(s_pending_navigation.kind!=SOCIAL_NONE || s_collection_requested)break;
            if(favorite_source && s_queue_job.state.account_mid!=bili_account_id())break;
            if(terminal.reason==PLAYER_NEXT && cur+1==s_npages && favorite_source && !have_next && favorite_retryable()) {
                /* Retry only on an explicit next tap, from the current cursor.
                 * A failed prefetch must not consume or erase this checkpoint. */
                s_queue_job.state=s_queue_job.current;have_next=favorite_prefetch();
                if(s_job_cancelled)break;
                if(!have_next){
                    favorite_queue_notice();playback_notice(s_busy);
                    if(s_queue_job.state.account_mid!=bili_account_id() || s_queue_job.state.last_result.login_required)break;
                    session.paused=terminal.paused;session.resume_seconds=terminal.resume_seconds;
                    continue;
                }
            }
            if(!app_session_next(&session,terminal,&cur,s_npages))break;
            s_busy_minimal=true;
        }
        if(s_pending_navigation.kind!=SOCIAL_NONE || net_is_shutting_down() || aptShouldClose())break;
        if(favorite_source && s_queue_job.state.account_mid!=bili_account_id()) {
            snprintf(s_busy,sizeof(s_busy),"账号已变化，收藏夹连播已停止");break;
        }
        if(s_collection_requested && !favorite_source) {
            s_collection_requested=false;
            int index=collection_index(&s_action_info,&s_action_video);
            if(index<0)break;
            video_info_free(&collection);collection=s_action_info;video_info_init(&s_action_info);
            collection_pos=index;video=collection.episodes[index];explicit_cid=true;
            continue_started=true;continue;
        }
        if(terminal.reason==PLAYER_PREVIOUS) {
            if(favorite_source&&fq_has_previous(&s_queue_job.current)) {
                if(browse_run(favorite_previous_job,NULL,"准备收藏夹上一条",true)!=0) {
                    if(!s_job_cancelled)favorite_queue_notice();
                    break;
                }
                video=s_queue_job.next;have_next=favorite_prefetch();
                if(s_job_cancelled)break;
                explicit_cid=false;continue_started=true;continue_paused=terminal.paused;continue;
            }
            if(collection_pos>0) {
                video=collection.episodes[--collection_pos];explicit_cid=true;
                continue_started=true;continue_paused=terminal.paused;continue;
            }
            break;
        }
        if(terminal.reason!=PLAYER_FINISHED&&terminal.reason!=PLAYER_NEXT)break;
        continue_paused=terminal.reason==PLAYER_NEXT&&terminal.paused;
        if(favorite_source && have_next) {
            video=s_queue_job.next;have_next=favorite_prefetch();
            if(s_job_cancelled)break;
            if(!have_next && s_queue_job.result!=FQ_END) favorite_queue_notice();
            explicit_cid=false;continue_started=true;continue;
        }
        if(collection_next) {
            video=collection.episodes[++collection_pos];explicit_cid=true;continue_started=true;continue;
        }
        break;
    }
playback_done:
    if(favorite_source && !s_job_cancelled && !net_is_shutting_down() && !aptShouldClose() &&
       s_queue_job.result!=FQ_END && s_queue_job.result!=FQ_NEXT) {
        favorite_queue_notice();
        playback_notice(s_busy);
    }
    video_info_free(&s_action_info);video_info_free(&collection);
    s_collection_requested=false;s_playing_favorite=false;s_busy_minimal=false;s_favorite_all_ready=false;
    memset(&s_queue_job,0,sizeof(s_queue_job));
    g_danmaku=settings_get("danmaku",1)!=0;player_set_pages(NULL,NULL,0,0);
    restore_browse();
    ui_trace_sync("exit-path: play_video done");
}
static int choose_favorite_order(void) {
    int focus=settings_get("fav_order",0);if(focus<0||focus>2)focus=0;
    const char *names[]={"顺序播放","随机播放","循环播放"};
    const char *help[]={"从第一条按收藏夹顺序播放，播完停止。","整夹打乱顺序，一轮内每条播放一次。","按收藏夹顺序播放，播完回到第一条。"};
    while(aptMainLoop()&&!net_is_shutting_down()&&!aptShouldClose()) {
        hidScanInput();u32 down=hidKeysDown();if(down&KEY_B)return -1;
        if((down&KEY_UP)&&focus>0)focus--;
        if((down&KEY_DOWN)&&focus<2)focus++;
        touchPosition t={0};if(down&KEY_TOUCH)hidTouchRead(&t);
        ui_begin();ui_text_clipped(10,24,UI_SHARP,UI_COL_TEXT,s_fav_title,380);
        ui_text_clipped(10,76,UI_SHARP,UI_COL_TEXT,help[focus],380);
        ui_text(10,124,UI_SHARP,UI_COL_DIM,"随机上限1000条（含失效视频）");
        ui_text(10,158,UI_SHARP,UI_COL_DIM,"开始后自动启用连播。");
        ui_begin_bottom();int hit=-1;
        for(int i=0;i<3;i++)if(ui_button(10,20+i*52,300,42,names[i],i==focus?UI_COL_ACCENT:UI_COL_SEL,down&KEY_TOUCH,t.px,t.py))hit=i;
        bool back=ui_button(10,180,300,30,"返回",UI_COL_SEL,down&KEY_TOUCH,t.px,t.py);
        ui_text(10,218,UI_SHARP,UI_COL_DIM,"上下选模式 A开始 B返回");ui_end();
        if(back)return -1;
        if(down&KEY_A)hit=focus;
        if(hit>=0){settings_set("fav_order",hit);return hit;}
    }
    return -1;
}
static void play_favorite_all(void) {
    if(s_mode!=MODE_FAV||s_fav_id<=0)return;
    thumb_stop();int order=choose_favorite_order();
    if(order<0){restore_browse();return;}
    s_favorite_all_order=(FqOrder)order;
    memset(&s_queue_job,0,sizeof s_queue_job);
    int result=browse_run(favorite_all_job,NULL,"准备收藏夹播放全部",true);
    if(s_job_cancelled||net_is_shutting_down()||aptShouldClose()){memset(&s_queue_job,0,sizeof s_queue_job);restore_browse();return;}
    if(result!=0) {
        favorite_queue_notice();
        playback_notice(s_busy);memset(&s_queue_job,0,sizeof s_queue_job);restore_browse();return;
    }
    BiliVideo first=s_queue_job.next;
    s_favorite_all_ready=true;settings_set("end_mode",PLAYER_END_CONTINUE);
    play_video(first,true);
}
static void play_selected(void) {
    if(s_sel<0 || s_sel>=s_count)return;
    BiliVideo video=s_list[s_sel];
    if(video.is_pgc) {
        thumb_stop();s_busy_minimal=true;
        PgcSeries series={0};PgcEpisode episode={0};bool picked=false;
        if(video.ep_id>0)picked=pgc_browse_episode(video.ep_id,browse_run,&series,&episode);
        else if(video.season_id>0)picked=pgc_browse_season(video.season_id,browse_run,&series,&episode);
        else playback_notice("番剧历史缺少集ID，请从番剧搜索进入");
        if(picked)play_pgc(series,episode);
        s_busy[0]=0;s_busy_minimal=false;restore_browse();
    } else play_video(video,s_mode==MODE_FAV);
}
typedef struct { PgcEpisode episode;PgcPlayback playback;PgcSeason season;BiliApiResult result; } PgcJob;
static PgcJob s_pgc;
static int pgc_season_job(void *unused) {
    (void)unused;s_pgc.result=pgc_episode_season(s_pgc.episode.ep_id,&s_pgc.season);
    return s_pgc.result.outcome==BILI_API_SUCCESS?0:-1;
}
static int pgc_play_job(void *unused) {
    (void)unused;
    g_qn=settings_get("qn",QN_360)==QN_480?QN_480:QN_360;
    s_pgc.result=pgc_playurl_quality(&s_pgc.episode,g_qn,&s_pgc.playback);
    /* A requested 480P may only expose incompatible DASH on this service.
     * Retry the known compatible 360P path, with the same full-length and
     * authorization checks; never turn preview/DRM into a playable result. */
    if(g_qn==QN_480 && !s_job_cancelled && !net_is_shutting_down() && !aptShouldClose() &&
       (s_pgc.result.outcome!=BILI_API_SUCCESS ||
        s_pgc.playback.status==PGC_PLAY_DASH || s_pgc.playback.status==PGC_PLAY_MULTISEG ||
        s_pgc.playback.status==PGC_PLAY_UNSUPPORTED))
        s_pgc.result=pgc_playurl_quality(&s_pgc.episode,QN_360,&s_pgc.playback);
    return s_pgc.result.outcome==BILI_API_SUCCESS &&
        s_pgc.playback.status==PGC_PLAY_FULL && s_pgc.playback.url[0]?0:-1;
}
static void playback_notice(const char *message) {
    while(aptMainLoop() && !net_is_shutting_down() && !aptShouldClose()) {
        hidScanInput();u32 down=hidKeysDown();
        ui_begin();ui_text_clipped(10,40,UI_SHARP,UI_COL_TEXT,message,380);
        ui_begin_bottom();ui_text(10,218,UI_SHARP,UI_COL_DIM,"A确认 B返回");ui_end();
        if(down&(KEY_B|KEY_A))break;
    }
}
/* Player pages are a 200-entry window; the season queue keeps all episodes.
 * Picking episode251 must never fall outside the playable mapping. */
static int pgc_window(const int *mapping,int count,int current,int *start) {
    *start=(current/MAX_PAGES)*MAX_PAGES;
    int visible=count-*start;if(visible>MAX_PAGES)visible=MAX_PAGES;
    for(int i=0;i<visible;i++) {
        PgcEpisode *e=&s_pgc.season.episodes[mapping[*start+i]];
        s_pages[i]=(BiliPage){.cid=e->cid,.page=*start+i+1,.duration=(int)(e->duration_ms/1000)};
        snprintf(s_pg_label[i],sizeof(s_pg_label[i]),"%.71s",e->title);
        s_pg_labelp[i]=s_pg_label[i];s_pg_dur[i]=(int)(e->duration_ms/1000);
    }
    s_npages=visible;return current-*start;
}
static void play_pgc(PgcSeries series,PgcEpisode selected) {
    thumb_stop();s_busy_minimal=true;memset(&s_pgc,0,sizeof(s_pgc));s_pgc.episode=selected;
    if(browse_run(pgc_season_job,NULL,"读取番剧选集",true)!=0 || net_is_shutting_down() || aptShouldClose()) {
        pgc_season_free(&s_pgc.season);return;
    }
    int *mapping=calloc((size_t)s_pgc.season.count,sizeof(*mapping));
    if(!mapping){pgc_season_free(&s_pgc.season);playback_notice("选集内存不足，未开始播放");return;}
    int n=0,cur=-1;
    for(int i=0;i<s_pgc.season.count;i++) {
        PgcEpisode *e=&s_pgc.season.episodes[i];
        if(e->extra!=selected.extra || (e->extra && strcmp(e->group,selected.group)))continue;
        mapping[n]=i;if(e->ep_id==selected.ep_id)cur=n;n++;
    }
    if(cur<0 || !n){free(mapping);pgc_season_free(&s_pgc.season);playback_notice("未能定位这一集，未开始播放");return;}
    AppPlaybackSession session;app_session_init(&session,g_autoplay);
    video_info_init(&s_action_info);s_playing_favorite=false;s_collection_requested=false;
    for(;;) {
        int window_start=0,localcur=pgc_window(mapping,n,cur,&window_start);
        s_pgc.episode=s_pgc.season.episodes[mapping[cur]];
        PgcEpisode *e=&s_pgc.episode;
        BiliVideo video={.aid=e->aid,.cid=e->cid,.pages=1,.duration=(int)(e->duration_ms/1000),.views=-1};
        snprintf(video.bvid,sizeof(video.bvid),"%s",e->bvid);snprintf(video.title,sizeof(video.title),"%.120s | %.70s",series.title,e->title);
        s_action_video=video;
        if(!session.started && !wait_for_play(&video,&localcur,&session))break;
        cur=window_start+localcur;
        s_pgc.episode=s_pgc.season.episodes[mapping[cur]];e=&s_pgc.episode;
        video.aid=e->aid;video.cid=e->cid;snprintf(video.bvid,sizeof(video.bvid),"%s",e->bvid);
        snprintf(video.title,sizeof(video.title),"%.120s | %.70s",series.title,e->title);s_action_video=video;
        if(browse_run(pgc_play_job,NULL,"解析正版播放地址",true)!=0) {
            if(!s_job_cancelled && !net_is_shutting_down() && !aptShouldClose()) {
                char reason[256];snprintf(reason,sizeof(reason),"%s：%.140s",pgc_play_message(s_pgc.playback.status),bili_last_error());playback_notice(reason);
            }
            break;
        }
        if(s_job_cancelled || net_is_shutting_down() || aptShouldClose())break;
        g_danmaku=settings_get("danmaku",1)!=0;
        if(g_danmaku)dm_ensure_async(e->cid);
        sub_free();
        player_set_meta(e->aid,e->cid,e->bvid);player_set_prefs(g_danmaku,g_force_sw,s_pgc.playback.quality==32?32:16);
        player_set_pages(s_pg_labelp,s_pg_dur,s_npages,localcur);
        player_set_navigation(PLAYER_NAV_EPISODES,cur>0,cur+1<n);
        PlayerStartOptions options={session.paused,cur+1<n && !s_pgc.season.truncated,session.resume_seconds};
        s_busy_minimal=false;s_busy[0]=0;
        PlayerResult result=player_play_session(s_pgc.playback.url,video.title,options);
        dm_free();sub_free();
        if(result.reason==PLAYER_PAGE)result.page_index+=window_start;
        if(s_pending_navigation.kind!=SOCIAL_NONE || !app_session_next(&session,result,&cur,n))break;
    }
    free(mapping);pgc_season_free(&s_pgc.season);video_info_free(&s_action_info);player_set_pages(NULL,NULL,0,0);
    s_collection_requested=false;s_busy_minimal=false;
}
static void process_pending_navigation(void) {
    SocialSelection selection=s_pending_navigation;memset(&s_pending_navigation,0,sizeof(s_pending_navigation));
    if(net_is_shutting_down() || aptShouldClose())return;
    thumb_stop();s_busy_minimal=true;
    if(selection.kind==SOCIAL_UP)show_up(&selection.profile);
    else if(selection.kind==SOCIAL_VIDEO)play_video(selection.video,false);
    else if(selection.kind==SOCIAL_PGC) {
        PgcSeries series;PgcEpisode episode;
        if(pgc_browse_episode(selection.ep_id,browse_run,&series,&episode))play_pgc(series,episode);
    }
    restore_browse();
}

/* APT 事件回调(HOME/睡眠/退出)。只做置标志这类轻活,别在回调里干重活 */
static void apt_hook_cb(APT_HookType hook, void *param) {
	(void)param;
	if (hook == APTHOOK_ONSUSPEND || hook == APTHOOK_ONSLEEP) {
		player_notify_suspend();
		/* 封面下载也停一下并掐掉在途那个,否则按 HOME 要等它跑完 */
		thumb_notify_suspend(1);
	} else if (hook == APTHOOK_ONRESTORE || hook == APTHOOK_ONWAKEUP) {
		thumb_notify_suspend(0);
	} else if (hook == APTHOOK_ONEXIT) {
		/* 【HOME → X 卡在 "Closing software" 的解法】
		 * 系统要关掉我们,而 aptMainLoop() 只有主循环转起来才会返回 false。
		 * 主线程要是正卡在一个同步 HTTP 请求里(列表、播放地址都是主线程
		 * 发的,httpc 没有超时),主循环就回不来,系统只能一直等。
		 * 所以在这里把网络整个掐掉并封死:在途的立刻失败,之后的一律拒绝,
		 * 主线程于是很快回到循环、看到 false、走正常清理。
		 * 这一步不可逆 —— 无所谓,ONEXIT 之后不会再回来了。 */
		net_shutdown_begin();
		thumb_notify_suspend(1);
		player_notify_suspend();
	}
}

int main(void) {
	/* 界面本来就该跑在满速上,不必等到播视频才提。Old3DS 上是空操作。
	 *
	 * 【别顺手在这儿加 APT_SetAppCpuTimeLimit】曾经加过,理由是「启动后
	 * 到第一次播放之前界面很卡」。后来实测证明**那个理由是错的** ——
	 * 用固定计算量了一把,播放前后耗时完全一样,主频从头到尾没变过。
	 * 真凶是封面缓存扫描线程在猛敲文件系统(见 thumb.c)。
	 * 而 SetAppCpuTimeLimit 是给「应用要用系统核」用的,常驻着反而可能
	 * 挤占 HOME 菜单所在的那个核心 —— 它留在 play_stream 里就够了。 */
	osSetSpeedupEnable(true);
	ui_init();
	/* HOME/睡眠钩子:挂起时暂停播放。不挂这个钩子的话,按 HOME 后
	 * 下载和解码线程照跑,HOME 菜单卡、回来更卡 */
	{
		static aptHookCookie s_apt_cookie;
		aptHook(&s_apt_cookie, apt_hook_cb, NULL);
	}
	/* 线性内存总量在这里报一次。MVD 工作缓冲、视频输出、音频缓冲全从这里出,
	 * "linear alloc failed" 时先回头看这个数就知道是天生不够还是被吃光了。
	 * 注:.3dsx 走 Homebrew Launcher 拿到的内存通常比装成 .cia 少不少 */
	printf("linear heap free at boot: %luKB\n",
	       (unsigned long)(linearSpaceFree() / 1024));
	/* 普通堆探针:从 16MB 往下折半试 malloc,量出最大可分配块。
	 * "字体 8MB 装不装得下、ffmpeg 够不够用"直接看这个数 */
	{
		size_t probe = 16 * 1024 * 1024;
		void *pp = NULL;
		while (probe >= 64 * 1024 && !(pp = malloc(probe))) probe /= 2;
		if (pp) free(pp);
		ui_trace("boot: linear=%luKB heap-maxblock=%luKB",
		         (unsigned long)(linearSpaceFree() / 1024),
		         (unsigned long)(pp ? probe / 1024 : 0));
	}
	ime_init();   /* 加载拼音词库(约 1MB,失败则输入退化为英文) */
	player_set_login_cb(login_cb);
	/* 开流阶段沿用列表页那一屏,状态写在状态条上(见 player.h) */
	player_set_busy_cb(player_busy_frame);
    player_set_actions_cb(video_actions);
    player_set_up_cb(video_up);

	/* 机型默认:New3DS 480P(有 MVD 硬解),老 3DS 360P(只能软解) */
	{
		bool n3 = false;
		APT_CheckNew3DS(&n3);
		g_qn = n3 ? QN_480 : QN_360;
	}
	/* 存档覆盖默认值。顺序要在机型判断**之后**:清晰度的默认因机型而异,
	 * settings_get 的 def 参数要拿到正确的机型默认 */
	settings_init();
    g_autoplay = settings_get("autoplay", 0) != 0;
	g_danmaku = settings_get("danmaku", g_danmaku ? 1 : 0) != 0;
	{	/* 存档里可能留着已经撤掉的 240P(qn=6),按无效处理回落默认 */
		int v = settings_get("qn", g_qn);
		g_qn = (v == QN_480 || v == QN_360) ? v : g_qn;
	}
	g_force_sw = settings_get("force_sw", g_force_sw ? 1 : 0) != 0;
	player_prefs_init();   /* 字幕开关/弹幕字号/字幕字号(存在 player.c) */

	printf("3Danmu\ninit network...\n");
	if (R_FAILED(net_init())) {
		printf("httpc init failed\n");
	} else {
		bili_init();
		printf("%s\n", bili_logged_in() ? "logged in" : "not logged in (touch login)");
	}

	load_list();
	ui_bottom_debug(false); /* 启动完成,切到操作提示面板 */


	while (aptMainLoop()) {
        if(s_pending_navigation.kind!=SOCIAL_NONE){process_pending_navigation();continue;}
		hidScanInput();
		u32 kDown = hidKeysDown();
		(void)0; /* kRepeat 已弃用:上下键改为 单击+长按连续滚动 */
		touchPosition tp = { 0, 0 };
		bool touched = (kDown & KEY_TOUCH) != 0;
		if (touched) hidTouchRead(&tp);

		/* 日志页只用触摸:拖动滚动、双击退出(都在 ui_draw_log 里)。
		 * 方向键/摇杆一律不接管 —— 它们在列表页是选视频用的,
		 * 让同一个键在两个场景做不同的事只会误触 */

		if (s_in_settings) {
			ui_begin();
			/* 松手帧 hidTouchRead 拿不到坐标,而点选正是在松手时判定的 ——
			 * 这里把「按住」的坐标一路带进去,由列表自己记住最后一次 */
			bool held_t = (hidKeysHeld() & KEY_TOUCH) != 0;
			bool up_t = (hidKeysUp() & KEY_TOUCH) != 0;
			touchPosition tps = { 0, 0 };
			if (held_t) hidTouchRead(&tps);
			draw_settings(touched, held_t, up_t, tps.px, tps.py);
			if (ui_console_active()) {
				if (ui_draw_log(touched, held_t, tps.px, tps.py)) {
					s_debug_ui = false;
					ui_bottom_debug(false);
				}
			}
			ui_end();
			if (s_debug_ui && !ui_console_active())
				ui_bottom_debug(true);
			if (kDown & KEY_B) s_in_settings = false;
			continue;
		}

        if (kDown & KEY_B) {
            if(s_mode==MODE_WATCHLATER) {
                show_personal();
            } else if(s_mode==MODE_USER_VIDEOS) {
                BiliProfile previous=s_user;
                if(!show_up(&previous) && !net_is_shutting_down()) {
                    s_mode=MODE_RECOMMEND;s_page=1;s_count=0;load_list();
                }
            } else if (s_mode == MODE_FAV) {
                if (choose_favorite_folder()) {
                    s_count=0; s_page=1; load_list();
                } else if (!net_is_shutting_down()) {
                    s_mode=MODE_RECOMMEND; s_page=1; s_count=0; load_list();
                }
            } else if (s_mode != MODE_RECOMMEND) {
                s_mode=MODE_RECOMMEND; s_hl_mode=-1; s_page=1;
                load_list();
            }
        }
		if ((kDown & KEY_START) &&
		    !(hidKeysHeld() & KEY_SELECT)) /* START+SELECT 是强制软解组合键 */
			break;
		if (!ui_console_active())
		{	/* 选择与滚动。
			 * 十字键:单击移一行(目标缓动滑入);按住超过 300ms 进入
			 * "连续滚动"——视口匀速走、选中项贴边跟随,而不是逐行连发。
			 * 逐行连发即使做了缓动也是节奏性的"平滑跳",体验仍是跳。
			 * 调试台开着时整块跳过:那会儿方向键/摇杆不该动任何东西,
			 * 日志页只用触摸(拖动滚动、双击退出) */
			static u64 hold_t0 = 0;
			if (kDown & KEY_DOWN && s_sel + 1 < s_count) {
				s_sel++;
				hold_t0 = osGetTime();
				float bot = (s_sel + 1) * ROW_H;
				if (bot > s_scroll_t + LIST_H) s_scroll_t = bot - LIST_H;
			}
			if (kDown & KEY_UP && s_sel > 0) {
				s_sel--;
				hold_t0 = osGetTime();
				float top = s_sel * ROW_H;
				if (top < s_scroll_t) s_scroll_t = top;
			}
			u32 held = hidKeysHeld();
			bool long_hold = (held & (KEY_UP | KEY_DOWN)) &&
			                 osGetTime() - hold_t0 > 300;
			if (long_hold && (held & KEY_DOWN)) {
				s_scroll_t += 4.5f;               /* 匀速下滚 */
				int last = (int)((s_scroll_t + LIST_H) / ROW_H) - 1;
				if (last >= s_count) last = s_count - 1;
				if (last > s_sel) s_sel = last;   /* 选中项贴下边缘跟随 */
			}
			if (long_hold && (held & KEY_UP)) {
				s_scroll_t -= 4.5f;               /* 匀速上滚 */
				int firstv = (int)((s_scroll_t + ROW_H - 1) / ROW_H);
				if (firstv < 0) firstv = 0;
				if (firstv < s_sel) s_sel = firstv;  /* 贴上边缘跟随 */
			}
			/* 左摇杆:匀速滚动,不动选中项 */
			circlePosition cp;
			hidCircleRead(&cp);
			if (cp.dy > 24 || cp.dy < -24)
				s_scroll_t -= (float)cp.dy * 0.055f;
			/* 夹取 + 缓动 */
			float maxs = s_count * ROW_H - LIST_H;
			if (maxs < 0) maxs = 0;
			if (s_scroll_t < 0) s_scroll_t = 0;
			if (s_scroll_t > maxs) s_scroll_t = maxs;
			s_scroll += (s_scroll_t - s_scroll) * 0.35f;
			if (s_scroll - s_scroll_t < 0.5f && s_scroll_t - s_scroll < 0.5f)
				s_scroll = s_scroll_t;
		}
        if (!ui_console_active()) {
		if (kDown & KEY_R) {
			if (s_page_end && s_page >= s_page_end) {
				/* 已经知道到底了:不再发那次注定为空的请求。
				 * 提示照给 —— 按了没反应比按了说"到底了"更让人困惑。 */
				snprintf(s_busy, sizeof(s_busy), "已经是最后一页");
			} else {
				s_page++;
				load_list();
			}
		}
		if (kDown & KEY_L && s_page > 1) { s_page--; load_list(); }
		if ((kDown & (KEY_ZL | KEY_ZR)) ||
		    ((kDown & KEY_SELECT) && !(hidKeysHeld() & KEY_START))) {
			/* 频道循环只含 热门↔推荐;历史/收藏走下屏独立按钮。
			 * SELECT 与 ZL/ZR 同功能,照顾没有 ZL/ZR 的老机型 */
			s_mode = (s_mode == MODE_POPULAR) ? MODE_RECOMMEND : MODE_POPULAR;
			s_hl_mode = -1;
			s_page = 1;
			load_list();
		}
		if (kDown & KEY_Y) { do_search(); continue; }
		if (kDown & KEY_X) { home_watchlater(); continue; }
		if (kDown & KEY_A) {
			if (s_count == 0 && s_list_failed) {   /* Failed load only; empty success is not an error. */
				printf("retrying...\n");
				bili_init();
				load_list();
			} else {
				play_selected();
				/* 【播放中 HOME→X 卡死的最后一块】播放器是从主循环这一格
				 * 里调进去的。被系统关闭时,播放器自己退得很干净(实测
				 * 全链路 150ms),但返回后**本轮循环还剩大半格没走完** ——
				 * 接下来会照常画一帧列表页,ui_end 里要等 VBlank,而退出
				 * 状态下 GSP 的 VBlank 事件可能永远不来,主线程就吊死在
				 * 那里,永远轮不到下一次 aptMainLoop() 返回 false。
				 * (日志指纹:最后一行是 exit-path: play_video done,
				 * 而 exit: thumb_exit 永远没出现。)
				 * 所以这里直接跳出主循环,别再画这最后一帧。 */
				if (net_is_shutting_down() || aptShouldClose()) {
					/* aptShouldClose 直接问 APT,不吃钩子竞态的亏 */
					net_shutdown_begin();
					ui_trace_sync("exit: bail after playback");
					break;
				}
			}
		}
        }

		/* 同样的兜底给搜索/登录:它们内部也各有一个 aptMainLoop 子循环,
		 * 被关闭时从里面返回后,剩下的这半格循环同样不能再画帧。 */
		if (net_is_shutting_down() || aptShouldClose()) {
			net_shutdown_begin();
			ui_trace_sync("exit: bail before draw");
			break;
		}


		ListActions act = { 0 };
		draw_list();
		if (ui_console_active()) {
			touchPosition th;
			hidTouchRead(&th);
			bool held = (hidKeysHeld() & KEY_TOUCH) != 0;
			if (ui_draw_log(touched, held, th.px, th.py)) {
				s_debug_ui = false;
				ui_bottom_debug(false);
			}
		} else {
			draw_bottom_list(touched, tp.px, tp.py, &act);
		}
		ui_end();

		if (act.login) {
			do_login();
			if (s_pending_mode >= 0) {
				s_mode = (ListMode)s_pending_mode;
				s_pending_mode = -1;
				s_page = 1;
				load_list();
			}
		}
		if(act.personal) { show_personal();continue; }
        if(act.watch_add) { home_watchlater();continue; }
        if(act.play_all){play_favorite_all();continue;}
        if(act.up_profile){show_up(&s_user);continue;}
        if(act.up_follow){
            thumb_stop();s_busy_minimal=true;
            if(bili_logged_in() || login_cb())s_user_relation=social_relationship_ui(s_user.mid,s_user.name,browse_run);
            restore_browse();continue;
        }
        if (act.settings) { s_in_settings = true; ui_list_reset(); }
		if (act.popular) {
			s_hl_mode = -1;
			if (s_mode != MODE_POPULAR) { s_mode = MODE_POPULAR; s_page = 1; load_list(); }
		}
		if (act.recommend) {
			s_hl_mode = -1;
			if (s_mode != MODE_RECOMMEND) { s_mode = MODE_RECOMMEND; s_page = 1; load_list(); }
		}
		if (act.hist || act.fav) {
			ListMode want = act.hist ? MODE_HISTORY : MODE_FAV;
			if (!bili_logged_in())
				do_login();          /* 未登录:直接拉起扫码 */
			if (s_pending_mode >= 0) {           /* 登录界面里改去别的频道 */
				s_mode = (ListMode)s_pending_mode;
				s_pending_mode = -1;
				s_page = 1;
				load_list();
			} else if (bili_logged_in()) {       /* 登录成功才切过去 */
                if (want != MODE_FAV || choose_favorite_folder()) {
                    s_mode = want;
                    s_page = 1;
                    s_count = 0;
                    load_list();
                }
			}
			s_hl_mode = -1;   /* 结束:高亮回落到真实频道 */
		}
	}

	/* 逐步落盘:万一还是卡在 "Closing software",trace.log 的最后一行
	 * 就指出卡在哪一步。异步日志在进程被杀时来不及写,所以用同步版。 */
	ui_trace_sync("exit: thumb_exit");
	thumb_exit();
	ui_trace_sync("exit: net_exit");
	net_exit();
	/* 放在 net_exit 之后:tls 只在扫码登录时用,退出时它一定是空闲的,
	 * 不会像 httpc 那样有请求卡在里面 */
	ui_trace_sync("exit: tls_exit");
	tls_exit();
	ui_trace_sync("exit: ime_exit");
	ime_exit();
	ui_trace_sync("exit: ui_exit");
	ui_exit();
	return 0;
}
