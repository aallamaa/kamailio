#include "../../parser/parse_to.h"
#include "../../parser/parse_uri.h" 
#include "../../parser/parse_content.h"
#include "../../parser/parse_from.h"
#include "../../ut.h"
#include "worker.h"
#include "peer.h"
#include "message.h"

int handle_dmq_message(struct sip_msg* msg, char* str1, char* str2) {
	dmq_peer_t* peer;
	if ((parse_sip_msg_uri(msg) < 0) || (!msg->parsed_uri.user.s)) {
			LM_ERR("cannot parse msg URI\n");
			return -1;
	}
	LM_DBG("handle_dmq_message [%.*s %.*s] [%s %s]\n",
	       msg->first_line.u.request.method.len, msg->first_line.u.request.method.s,
	       msg->first_line.u.request.uri.len, msg->first_line.u.request.uri.s,
	       ZSW(str1), ZSW(str2));
	/* the peer id is given as the userinfo part of the request URI */
	peer = find_peer(msg->parsed_uri.user);
	if(!peer) {
		LM_DBG("no peer found for %.*s\n", msg->parsed_uri.user.len, msg->parsed_uri.user.s);
		return 0;
	}
	LM_DBG("handle_dmq_message peer found: %.*s\n", msg->parsed_uri.user.len, msg->parsed_uri.user.s);
	add_dmq_job(msg, peer);
	return 0;
}