/* Real favorite modal with fake input/render/transport. No account or device. */
#include <stdio.h>
#include <string.h>
#define KEY_X (1u<<24)
#include "../source/video_actions_ui.c"
#undef printf
static int frame,limit=64,checks,failures,folder_count,reads,writes;
static u32 keys[64];
static int touch_folder[64],first_row[64],last_row[64],row_count[64],page_number[64],page_total[64];
static bool members[VIDEO_FOLDER_MAX],written[VIDEO_FOLDER_MAX],truncated,read_failed,hints[64],bottom;
#define CHECK(x) do { checks++;if(!(x)){ failures++;printf("FAIL %d frame%d: %s\n",__LINE__,frame,#x); } } while(0)
bool aptMainLoop(void){return frame<limit;}
bool aptShouldClose(void){return false;}
bool net_is_shutting_down(void){return false;}
void hidScanInput(void){frame++;}
u32 hidKeysDown(void){return keys[frame] | (touch_folder[frame]?KEY_TOUCH:0);}
u32 hidKeysHeld(void){return KEY_TOUCH;}
void hidTouchRead(touchPosition*t){t->px=20;t->py=20;}
void ui_begin(void){bottom=false;}
void ui_begin_bottom(void){bottom=true;}
void ui_end(void){}
void ui_text(float x,float y,float scale,uint32_t color,const char *text){
    (void)x;(void)scale;
    if(!bottom && y==108){int changes=0;const char *p=strstr(text,"；第 ");if(p)sscanf(p+strlen("；第 "),"%d/%d",&page_number[frame],&page_total[frame]);(void)changes;}
    if(bottom && y==218){hints[frame]=strstr(text,"翻页") && strstr(text,"触屏") && strstr(text,"X") && strstr(text,"B") && strstr(text,"L") && strstr(text,"R") && color==UI_COL_DIM;
        CHECK(!strstr(text,"A切换") && !strstr(text,"上下选夹"));}
}
void ui_text_clipped(float x,float y,float scale,uint32_t color,const char *text,float w){(void)x;(void)y;(void)scale;(void)color;(void)text;(void)w;}
bool ui_button(float x,float y,float w,float h,const char *text,uint32_t color,bool touch,float tx,float ty){
    (void)x;(void)w;(void)touch;(void)tx;(void)ty;
    CHECK(y+h<=212);
    char mark;int id;
    if(sscanf(text,"[%c] Folder%d",&mark,&id)==2){
        if(!row_count[frame])first_row[frame]=id;
        last_row[frame]=id;row_count[frame]++;
        CHECK((mark=='x')==(color==UI_COL_ACCENT));
        return touch_folder[frame]==id;
    }
    return false;
}
bool bili_logged_in(void){return true;}
int64_t bili_account_id(void){return 123;}
BiliApiResult video_folders(int64_t aid,BiliFavFolder*out,int max,BiliPageInfo *info){
    CHECK(aid==99 && max==VIDEO_FOLDER_MAX);reads++;memset(info,0,sizeof(*info));
    if(read_failed)return (BiliApiResult){.outcome=BILI_API_UNKNOWN};
    info->count=folder_count;info->truncated=truncated;
    for(int i=0;i<folder_count;i++){
        out[i]=(BiliFavFolder){.id=i+1,.contains=members[i],.membership_known=true};
        snprintf(out[i].title,sizeof out[i].title,"Folder%02d",i+1);
    }
    return (BiliApiResult){.outcome=BILI_API_SUCCESS};
}
BiliApiResult video_favorite_delta_once(int64_t aid,const BiliFavFolder *folders,const bool *selected,int count){
    CHECK(aid==99 && count==folder_count);writes++;
    for(int i=0;i<count;i++){CHECK(folders[i].id==i+1);members[i]=written[i]=selected[i];}
    return (BiliApiResult){.outcome=BILI_API_SUCCESS};
}
BiliApiResult video_coins(int64_t a,int*b){(void)a;(void)b;return (BiliApiResult){.outcome=BILI_API_UNKNOWN};}
BiliApiResult video_coin_once(int64_t a,int b,bool c){(void)a;(void)b;(void)c;return (BiliApiResult){.outcome=BILI_API_UNKNOWN};}
BiliApiResult bili_like_status(int64_t a,BiliLikeState*b){(void)a;(void)b;return (BiliApiResult){.outcome=BILI_API_UNKNOWN};}
BiliApiResult bili_like_set(int64_t a,bool b){(void)a;(void)b;return (BiliApiResult){.outcome=BILI_API_UNKNOWN};}
static int runner(int(*fn)(void*),void *arg,const char *message,bool cancellable){
    (void)message;int before=writes;int r=fn(arg);if(writes>before)CHECK(!cancellable);return r;
}
static void reset(int count){
    frame=reads=writes=0;folder_count=count;truncated=read_failed=false;
    memset(keys,0,sizeof keys);memset(touch_folder,0,sizeof touch_folder);
    memset(first_row,0,sizeof first_row);memset(last_row,0,sizeof last_row);memset(row_count,0,sizeof row_count);
    memset(page_number,0,sizeof page_number);memset(page_total,0,sizeof page_total);memset(hints,0,sizeof hints);
    memset(members,0,sizeof members);memset(written,0,sizeof written);members[0]=count>0;
    keys[12]=KEY_B;
}
static void open(void){BiliVideo v={.aid=99};video_account_action_ui(&v,2,runner,NULL);}
int main(void){
    reset(10);
    keys[2]=KEY_DOWN;keys[3]=KEY_R;keys[4]=KEY_DOWN|KEY_R;keys[5]=KEY_DOWN;
    keys[6]=KEY_UP;keys[7]=KEY_L;keys[8]=KEY_UP|KEY_L;keys[9]=KEY_UP;keys[10]=KEY_L;
    open();
    int expected[]={0,1,4,7,10,10,7,4,1,1,1,1};
    for(int i=1;i<=11;i++){
        CHECK(first_row[i]==expected[i]);CHECK(row_count[i]==(expected[i]==10?1:3));
        CHECK(page_number[i]==(expected[i]-1)/3+1 && page_total[i]==4);CHECK(hints[i]);
    }
    CHECK(!writes && reads==1);
    reset(8);
    touch_folder[1]=2;keys[2]=KEY_R;touch_folder[3]=4;keys[4]=KEY_DOWN;touch_folder[5]=7;
    keys[6]=KEY_L;keys[7]=KEY_A;keys[8]=KEY_X;keys[9]=KEY_X;keys[10]=KEY_X;keys[11]=KEY_B;
    open();
    CHECK(writes==1 && reads==2);
    for(int i=0;i<8;i++)CHECK(written[i]==(i==0||i==1||i==3||i==6));
    CHECK(first_row[2]==4 && first_row[4]==7 && first_row[6]==4);
    reset(1);keys[1]=KEY_UP|KEY_L;keys[2]=KEY_DOWN|KEY_R;keys[3]=KEY_A;keys[4]=KEY_X;keys[5]=KEY_X;open();
    CHECK(!writes && reads==1 && first_row[1]==1 && first_row[2]==1 && page_total[1]==1);
    reset(0);keys[1]=KEY_DOWN|KEY_R;keys[2]=KEY_UP|KEY_L;keys[3]=KEY_A|KEY_X;open();
    CHECK(!writes && !row_count[1] && page_total[1]==1);
    reset(100);keys[1]=KEY_DOWN;keys[2]=KEY_R;
    for(int i=3;i<40;i++)keys[i]=KEY_DOWN;
    keys[12]=0;keys[40]=KEY_B;open();
    CHECK(first_row[39]==100 && row_count[39]==1 && page_number[39]==34 && page_total[39]==34 && !writes);
    reset(4);truncated=true;touch_folder[1]=2;keys[2]=KEY_DOWN;touch_folder[3]=4;keys[4]=KEY_X;keys[5]=KEY_X;open();CHECK(!writes);
    reset(4);read_failed=true;keys[1]=KEY_DOWN|KEY_R;keys[2]=KEY_X;keys[3]=KEY_X;open();CHECK(!writes && !row_count[1]);
    printf("Favorite touch pages: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
