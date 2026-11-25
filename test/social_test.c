#include "social_api.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "bili_transport.h"
static int get_calls;
BiliApiResult bili_fetch_json(const char*a,const char**b,const char**c,int d,bool e,HttpResponse*f){get_calls++;(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return (BiliApiResult){.outcome=BILI_API_UNKNOWN};}
BiliApiResult bili_submit_json(const char*a,const char**b,const char**c,int d,HttpResponse*f){(void)a;(void)b;(void)c;(void)d;(void)f;return (BiliApiResult){.outcome=BILI_API_UNKNOWN};}
void net_response_free(HttpResponse*r){(void)r;}
int64_t bili_account_id(void){return 1;}
static const char relation[]="{\"code\":0,\"data\":{\"attribute\":6}}";
static const char duplicate[]="{\"code\":0,\"data\":{\"attribute\":2,\"attribute\":0}}";
static const char people[]="{\"code\": 0, \"data\": {\"total\": 1, \"list\": [{\"mid\": 42, \"uname\": \"UP\", \"sign\": \"hello\"}]}}";
static const char dynamic[]="{\"code\": 0, \"data\": {\"offset\": \"opaque-92233720368547758099\", \"has_more\": true, \"items\": [{\"id_str\": \"123\", \"type\": \"DYNAMIC_TYPE_AV\", \"modules\": {\"module_author\": {\"mid\": 42, \"name\": \"UP\"}, \"module_dynamic\": {\"desc\": {\"text\": \"Long text\\nNext line\"}, \"major\": {\"archive\": {\"bvid\": \"BV1234567890\", \"title\": \"Video\", \"aid\": \"123\"}}}}}]}}";
static const char opus[]="{\"code\": 0, \"data\": {\"offset\": \"\", \"has_more\": false, \"items\": [{\"id_str\": \"124\", \"type\": \"DYNAMIC_TYPE_DRAW\", \"modules\": {\"module_author\": {\"mid\": 42, \"name\": \"UP\"}, \"module_dynamic\": {\"major\": {\"opus\": {\"summary\": {\"text\": \"actual opus body\"}, \"pics\": [{\"url\": \"https://i0.hdslb.com/a.jpg\", \"width\": 300, \"height\": 200}]}}}}}]}}";
static const char reply[]="{\"code\": 0, \"data\": {\"cursor\": {\"is_end\": false, \"id\": 12, \"time\": 44}, \"items\": [{\"id\": 12, \"reply_time\": 44, \"item\": {\"source_content\": \"actual reply\", \"bvid\": \"BV1234567890\"}}]}}";
static const char unread[]="{\"code\": 0, \"data\": {\"reply\": 1, \"at\": 2, \"like\": 3, \"sys_msg\": 4}}";
#define PARSE(fn,body,...) fn(0,200,body,strlen(body),__VA_ARGS__)
static const char deleted[]="{\"code\": 0, \"data\": {\"offset\": \"\", \"has_more\": false, \"items\": [{\"id_str\": \"1\", \"type\": \"DYNAMIC_TYPE_FORWARD\", \"modules\": {\"module_author\": {\"mid\": 42, \"name\": \"UP\"}, \"module_dynamic\": {\"desc\": {\"text\": \"forward\"}}}, \"orig\": {\"type\": \"DYNAMIC_TYPE_NONE\"}}]}}";
static const char overflow[]="{\"code\": 0, \"data\": {\"offset\": \"opaque-next\", \"has_more\": true, \"items\": [{\"id_str\": \"1\", \"type\": \"DYNAMIC_TYPE_WORD\", \"modules\": {\"module_author\": {\"mid\": 42, \"name\": \"UP\"}, \"module_dynamic\": {\"desc\": {\"text\": \"body\"}}}}, {\"id_str\": \"1\", \"type\": \"DYNAMIC_TYPE_WORD\", \"modules\": {\"module_author\": {\"mid\": 42, \"name\": \"UP\"}, \"module_dynamic\": {\"desc\": {\"text\": \"body\"}}}}]}}";
static const char nested_forward[]="{\"code\": 0, \"data\": {\"offset\": \"\", \"has_more\": false, \"items\": [{\"id_str\": \"1\", \"type\": \"DYNAMIC_TYPE_FORWARD\", \"modules\": {\"module_author\": {\"mid\": 1, \"name\": \"Forward\"}, \"module_dynamic\": {\"desc\": {\"text\": \"forward prefix\"}}}, \"orig\": {\"id_str\": \"2\", \"type\": \"DYNAMIC_TYPE_FORWARD\", \"modules\": {\"module_author\": {\"mid\": 2, \"name\": \"Forward\"}, \"module_dynamic\": {\"desc\": {\"text\": \"forward prefix\"}}}, \"orig\": {\"id_str\": \"3\", \"type\": \"DYNAMIC_TYPE_DRAW\", \"modules\": {\"module_author\": {\"mid\": 42, \"name\": \"Leaf\"}, \"module_dynamic\": {\"desc\": {\"text\": \"description preserved\"}, \"major\": {\"opus\": {\"summary\": {\"text\": \"deep actual body\"}}}}}}}}]}}";
static const char missing_summary[]="{\"code\": 0, \"data\": {\"offset\": \"\", \"has_more\": false, \"items\": [{\"id_str\": \"3\", \"type\": \"DYNAMIC_TYPE_DRAW\", \"modules\": {\"module_author\": {\"mid\": 42, \"name\": \"Leaf\"}, \"module_dynamic\": {\"desc\": {\"text\": \"description preserved\"}, \"major\": {\"opus\": {}}}}}]}}";
static const char invalid_summary[]="{\"code\": 0, \"data\": {\"offset\": \"\", \"has_more\": false, \"items\": [{\"id_str\": \"3\", \"type\": \"DYNAMIC_TYPE_DRAW\", \"modules\": {\"module_author\": {\"mid\": 42, \"name\": \"Leaf\"}, \"module_dynamic\": {\"desc\": {\"text\": \"description preserved\"}, \"major\": {\"opus\": {\"summary\": {\"text\": 42}}}}}}]}}";
int main(void){SocialRelation r;BiliProfile people_out[2];BiliPageInfo pi;SocialDynamic *cards=calloc(2,sizeof(*cards));SocialPage page;SocialMessage *messages=calloc(2,sizeof(*messages));
assert(PARSE(social_parse_relation,relation,&r).outcome==BILI_API_SUCCESS&&r.following&&r.mutual);
assert(PARSE(social_parse_relation,duplicate,&r).outcome==BILI_API_UNKNOWN&&!r.known);
assert(social_parse_relation(-1,200,relation,strlen(relation),&r).outcome==BILI_API_UNKNOWN);
assert(PARSE(social_parse_people,people,1,people_out,2,&pi).outcome==BILI_API_SUCCESS&&pi.count==1&&people_out[0].mid==42);
assert(PARSE(social_parse_dynamics,dynamic,cards,2,&page).outcome==BILI_API_SUCCESS&&page.page.count==1&&page.page.has_more&&cards[0].target.kind==SOCIAL_VIDEO);
assert(!strcmp(page.next,"opaque-92233720368547758099")&&!strcmp(cards[0].body,"Long text\nNext line"));
assert(PARSE(social_parse_dynamics,opus,cards,2,&page).outcome==BILI_API_SUCCESS&&cards[0].image_count==1&&!strcmp(cards[0].body,"actual opus body"));
assert(PARSE(social_parse_messages,reply,SOCIAL_REPLIES,messages,2,&page).outcome==BILI_API_SUCCESS&&page.page.has_more&&!strcmp(page.next,"12,44")&&messages[0].target.kind==SOCIAL_VIDEO);
assert(PARSE(social_parse_messages,unread,SOCIAL_UNREAD,messages,2,&page).outcome==BILI_API_SUCCESS&&strstr(messages[0].body,"sys_msg: 4"));
assert(PARSE(social_parse_dynamics,relation,cards,2,&page).outcome==BILI_API_UNKNOWN&&page.page.count==0);
assert(PARSE(social_parse_dynamics,deleted,cards,2,&page).outcome==BILI_API_SUCCESS&&strstr(cards[0].body,"已删除")&&cards[0].target.kind==SOCIAL_NONE);
assert(PARSE(social_parse_dynamics,overflow,cards,1,&page).outcome==BILI_API_UNKNOWN&&page.page.count==0&&!page.next[0]);
for(int category=SOCIAL_AT;category<=SOCIAL_SYSTEM;category++){
assert(social_message_support_note((SocialMessageKind)category)!=NULL);
assert(social_messages((SocialMessageKind)category,"",messages,2,&page).outcome==BILI_API_REJECTED&&get_calls==0&&page.page.count==0);
assert(PARSE(social_parse_messages,reply,(SocialMessageKind)category,messages,2,&page).outcome==BILI_API_UNKNOWN&&page.page.count==0);
}
assert(PARSE(social_parse_dynamics,nested_forward,cards,2,&page).outcome==BILI_API_SUCCESS&&strstr(cards[0].body,"deep actual body"));
assert(PARSE(social_parse_dynamics,missing_summary,cards,2,&page).outcome==BILI_API_SUCCESS&&!strcmp(cards[0].body,"description preserved"));
assert(PARSE(social_parse_dynamics,invalid_summary,cards,2,&page).outcome==BILI_API_UNKNOWN&&page.page.count==0);
free(cards);free(messages);puts("social fixtures PASS");return 0;}
