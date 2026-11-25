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

