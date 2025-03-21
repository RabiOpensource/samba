/*
   Unix SMB/CIFS implementation.
   RPC pipe client

   Copyright (C) Tim Potter              2000
   Copyright (C) Rafal Szczesniak        2002
   Copyright (C) Guenther Deschner	 2008

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "includes.h"
#include "rpcclient.h"
#include "../libcli/auth/libcli_auth.h"
#include "../librpc/gen_ndr/ndr_lsa.h"
#include "../librpc/gen_ndr/ndr_lsa_c.h"
#include "rpc_client/cli_lsarpc.h"
#include "rpc_client/init_lsa.h"
#include "../libcli/security/security.h"

/* useful function to allow entering a name instead of a SID and
 * looking it up automatically */
static NTSTATUS name_to_sid(struct rpc_pipe_client *cli,
			    TALLOC_CTX *mem_ctx,
			    struct dom_sid *sid,
			    const char *name)
{
	struct policy_handle pol;
	enum lsa_SidType *sid_types;
	NTSTATUS status, result;
	struct dom_sid *sids;
	struct dcerpc_binding_handle *b = cli->binding_handle;

	/* maybe its a raw SID */
	if (strncmp(name, "S-", 2) == 0 &&
	    string_to_sid(sid, name)) {
		return NT_STATUS_OK;
	}

	status = rpccli_lsa_open_policy(cli, mem_ctx, True,
				     SEC_FLAG_MAXIMUM_ALLOWED,
				     &pol);
	if (!NT_STATUS_IS_OK(status))
		goto done;

	status = rpccli_lsa_lookup_names(cli, mem_ctx, &pol, 1, &name, NULL, 1, &sids, &sid_types);
	if (!NT_STATUS_IS_OK(status))
		goto done;

	dcerpc_lsa_Close(b, mem_ctx, &pol, &result);

	*sid = sids[0];

done:
	return status;
}

static void display_query_info_1(struct lsa_AuditLogInfo *r)
{
	d_printf("percent_full:\t%d\n", r->percent_full);
	d_printf("maximum_log_size:\t%d\n", r->maximum_log_size);
	d_printf("retention_time:\t%lld\n", (long long)r->retention_time);
	d_printf("shutdown_in_progress:\t%d\n", r->shutdown_in_progress);
	d_printf("time_to_shutdown:\t%lld\n", (long long)r->time_to_shutdown);
	d_printf("next_audit_record:\t%d\n", r->next_audit_record);
}

static void display_query_info_2(struct lsa_AuditEventsInfo *r)
{
	int i;
	d_printf("Auditing enabled:\t%d\n", r->auditing_mode);
	d_printf("Auditing categories:\t%d\n", r->count);
	d_printf("Auditsettings:\n");
	for (i=0; i<r->count; i++) {
		const char *val = audit_policy_str(talloc_tos(), r->settings[i]);
		const char *policy = audit_description_str(i);
		d_printf("%s:\t%s\n", policy, val);
	}
}

static void display_query_info_3(struct lsa_DomainInfo *r)
{
	struct dom_sid_buf buf;
	d_printf("Domain Name: %s\n", r->name.string);
	d_printf("Domain Sid: %s\n", dom_sid_str_buf(r->sid, &buf));
}

static void display_query_info_5(struct lsa_DomainInfo *r)
{
	struct dom_sid_buf buf;
	d_printf("Domain Name: %s\n", r->name.string);
	d_printf("Domain Sid: %s\n", dom_sid_str_buf(r->sid, &buf));
}

static void display_query_info_10(struct lsa_AuditFullSetInfo *r)
{
	d_printf("Shutdown on full: %d\n", r->shutdown_on_full);
}

static void display_query_info_11(struct lsa_AuditFullQueryInfo *r)
{
	d_printf("Shutdown on full: %d\n", r->shutdown_on_full);
	d_printf("Log is full: %d\n", r->log_is_full);
}

static void display_query_info_12(struct lsa_DnsDomainInfo *r)
{
	struct dom_sid_buf buf;
	d_printf("Domain NetBios Name: %s\n", r->name.string);
	d_printf("Domain DNS Name: %s\n", r->dns_domain.string);
	d_printf("Domain Forest Name: %s\n", r->dns_forest.string);
	d_printf("Domain Sid: %s\n", dom_sid_str_buf(r->sid, &buf));
	d_printf("Domain GUID: %s\n", GUID_string(talloc_tos(),
						      &r->domain_guid));
}

static void display_lsa_query_info(union lsa_PolicyInformation *info,
				   enum lsa_PolicyInfo level)
{
	switch (level) {
		case 1:
			display_query_info_1(&info->audit_log);
			break;
		case 2:
			display_query_info_2(&info->audit_events);
			break;
		case 3:
			display_query_info_3(&info->domain);
			break;
		case 5:
			display_query_info_5(&info->account_domain);
			break;
		case 10:
			display_query_info_10(&info->auditfullset);
			break;
		case 11:
			display_query_info_11(&info->auditfullquery);
			break;
		case 12:
			display_query_info_12(&info->dns);
			break;
		default:
			printf("can't display info level: %d\n", level);
			break;
	}
}

static NTSTATUS cmd_lsa_query_info_policy(struct rpc_pipe_client *cli,
					  TALLOC_CTX *mem_ctx,
					  int argc,
					  const char **argv)
{
	struct policy_handle pol;
	NTSTATUS status, result;
	union lsa_PolicyInformation *info = NULL;
	struct dcerpc_binding_handle *b = cli->binding_handle;

	uint32_t info_class = 3;

	if (argc > 2) {
		printf("Usage: %s [info_class]\n", argv[0]);
		return NT_STATUS_OK;
	}

	if (argc == 2)
		info_class = atoi(argv[1]);

	switch (info_class) {
	case 12: {
		union lsa_revision_info out_revision_info = {
			.info1 = {
				.revision = 0,
			},
		};
		uint32_t out_version = 0;

		status = dcerpc_lsa_open_policy_fallback(
			cli,
			mem_ctx,
			cli->srv_name_slash,
			true,
			SEC_FLAG_MAXIMUM_ALLOWED,
			&out_version,
			&out_revision_info,
			&pol,
			&result);
		if (any_nt_status_not_ok(status, result, &status)) {
			goto done;
		}

		status = dcerpc_lsa_QueryInfoPolicy2(b, mem_ctx,
						     &pol,
						     info_class,
						     &info,
						     &result);
		break;
	}
	default:
		status = rpccli_lsa_open_policy(cli, mem_ctx, True,
						SEC_FLAG_MAXIMUM_ALLOWED,
						&pol);

		if (!NT_STATUS_IS_OK(status))
			goto done;

		status = dcerpc_lsa_QueryInfoPolicy(b, mem_ctx,
						    &pol,
						    info_class,
						    &info,
						    &result);
	}

	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}
	status = result;
	if (NT_STATUS_IS_OK(result)) {
		display_lsa_query_info(info, info_class);
	}

	dcerpc_lsa_Close(b, mem_ctx, &pol, &result);

 done:
	return status;
}

/* Resolve a list of names to a list of sids */

static NTSTATUS cmd_lsa_lookup_names(struct rpc_pipe_client *cli,
				     TALLOC_CTX *mem_ctx,
				     int argc,
				     const char **argv)
{
	struct policy_handle pol;
	NTSTATUS status, result;
	struct dom_sid *sids;
	enum lsa_SidType *types;
	int i;
	struct dcerpc_binding_handle *b = cli->binding_handle;

	if (argc == 1) {
		printf("Usage: %s [name1 [name2 [...]]]\n", argv[0]);
		return NT_STATUS_OK;
	}

	status = rpccli_lsa_open_policy(cli, mem_ctx, True,
					LSA_POLICY_LOOKUP_NAMES,
					&pol);

	if (!NT_STATUS_IS_OK(status))
		goto done;

	status = rpccli_lsa_lookup_names(cli, mem_ctx, &pol, argc - 1,
				      (const char**)(argv + 1), NULL, 1, &sids, &types);

	if (!NT_STATUS_IS_OK(status) && NT_STATUS_V(status) !=
	    NT_STATUS_V(STATUS_SOME_UNMAPPED))
		goto done;

	status = NT_STATUS_OK;

	/* Print results */

	for (i = 0; i < (argc - 1); i++) {
		struct dom_sid_buf sid_str;
		printf("%s %s (%s: %d)\n",
		       argv[i + 1],
		       dom_sid_str_buf(&sids[i], &sid_str),
		       sid_type_lookup(types[i]),
		       types[i]);
	}

	dcerpc_lsa_Close(b, mem_ctx, &pol, &result);

 done:
	return status;
}

/* Resolve a list of names to a list of sids */

static NTSTATUS cmd_lsa_lookup_names_level(struct rpc_pipe_client *cli,
					   TALLOC_CTX *mem_ctx,
					   int argc,
					   const char **argv)
{
	struct policy_handle pol;
	NTSTATUS status, result;
	struct dom_sid *sids = NULL;
	enum lsa_SidType *types = NULL;
	int i, level;
	struct dcerpc_binding_handle *b = cli->binding_handle;

	if (argc < 3) {
		printf("Usage: %s [level] [name1 [name2 [...]]]\n", argv[0]);
		return NT_STATUS_OK;
	}

	status = rpccli_lsa_open_policy(cli, mem_ctx, True,
					LSA_POLICY_LOOKUP_NAMES,
					&pol);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}

	level = atoi(argv[1]);

	status = rpccli_lsa_lookup_names(cli, mem_ctx, &pol, argc - 2,
				      (const char**)(argv + 2), NULL, level, &sids, &types);

	if (!NT_STATUS_IS_OK(status) && NT_STATUS_V(status) !=
	    NT_STATUS_V(STATUS_SOME_UNMAPPED))
	{
		goto done;
	}

	status = NT_STATUS_OK;

	/* Print results */

	for (i = 0; i < (argc - 2); i++) {
		struct dom_sid_buf sid_str;
		printf("%s %s (%s: %d)\n",
		       argv[i + 2],
		       dom_sid_str_buf(&sids[i], &sid_str),
		       sid_type_lookup(types[i]),
		       types[i]);
	}

	dcerpc_lsa_Close(b, mem_ctx, &pol, &result);

 done:
	return status;
}

static NTSTATUS cmd_lsa_lookup_names4(struct rpc_pipe_client *cli,
				      TALLOC_CTX *mem_ctx, int argc,
				      const char **argv)
{
	NTSTATUS status, result;

	uint32_t num_names;
	struct lsa_String *names;
	struct lsa_RefDomainList *domains = NULL;
	struct lsa_TransSidArray3 sids;
	uint32_t count = 0;
	int i;
	struct dcerpc_binding_handle *b = cli->binding_handle;

	if (argc == 1) {
		printf("Usage: %s [name1 [name2 [...]]]\n", argv[0]);
		return NT_STATUS_OK;
	}

	ZERO_STRUCT(sids);

	num_names = argc-1;
	names = talloc_array(mem_ctx, struct lsa_String, num_names);
	NT_STATUS_HAVE_NO_MEMORY(names);

	for (i=0; i < num_names; i++) {
		init_lsa_String(&names[i], argv[i+1]);
	}

	status = dcerpc_lsa_LookupNames4(b, mem_ctx,
					 num_names,
					 names,
					 &domains,
					 &sids,
					 1,
					 &count,
					 0,
					 0,
					 &result);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}
	if (!NT_STATUS_IS_OK(result)) {
		return result;
	}

	if (sids.count != num_names) {
		return NT_STATUS_INVALID_NETWORK_RESPONSE;
	}

	for (i = 0; i < sids.count; i++) {
		struct dom_sid_buf sid_str;
		printf("%s %s (%s: %d)\n",
		       argv[i+1],
		       dom_sid_str_buf(sids.sids[i].sid, &sid_str),
		       sid_type_lookup(sids.sids[i].sid_type),
		       sids.sids[i].sid_type);
	}

	return status;
}

/* Resolve a list of SIDs to a list of names */

static NTSTATUS cmd_lsa_lookup_sids(struct rpc_pipe_client *cli, TALLOC_CTX *mem_ctx,
                                    int argc, const char **argv)
{
	struct policy_handle pol;
	NTSTATUS status, result;
	struct dom_sid *sids;
	char **domains;
	char **names;
	enum lsa_SidType *types;
	int i;
	struct dcerpc_binding_handle *b = cli->binding_handle;

	if (argc == 1) {
		printf("Usage: %s [sid1 [sid2 [...]]]\n", argv[0]);
		return NT_STATUS_OK;
	}

	status = rpccli_lsa_open_policy(cli, mem_ctx, True,
					LSA_POLICY_LOOKUP_NAMES,
					&pol);

	if (!NT_STATUS_IS_OK(status))
		goto done;

	/* Convert arguments to sids */

	sids = talloc_array(mem_ctx, struct dom_sid, argc - 1);

	if (!sids) {
		printf("could not allocate memory for %d sids\n", argc - 1);
		goto done;
	}

	for (i = 0; i < argc - 1; i++)
		if (!string_to_sid(&sids[i], argv[i + 1])) {
			status = NT_STATUS_INVALID_SID;
			goto done;
		}

	/* Lookup the SIDs */

	status = rpccli_lsa_lookup_sids(cli, mem_ctx, &pol, argc - 1, sids,
				     &domains, &names, &types);

	if (!NT_STATUS_IS_OK(status) && NT_STATUS_V(status) !=
	    NT_STATUS_V(STATUS_SOME_UNMAPPED))
		goto done;

	status = NT_STATUS_OK;

	/* Print results */

	for (i = 0; i < (argc - 1); i++) {
		struct dom_sid_buf sid_str;

		dom_sid_str_buf(&sids[i], &sid_str);
		if (types[i] == SID_NAME_DOMAIN) {
			printf("%s %s (%d)\n", sid_str.buf,
			       domains[i] ? domains[i] : "*unknown*",
			       types[i]);
		} else {
			printf("%s %s\\%s (%d)\n", sid_str.buf,
			       domains[i] ? domains[i] : "*unknown*",
			       names[i] ? names[i] : "*unknown*",
			       types[i]);
		}
	}

	dcerpc_lsa_Close(b, mem_ctx, &pol, &result);

 done:
	return status;
}

static NTSTATUS cmd_lsa_lookup_sids_level(struct rpc_pipe_client *cli,
					  TALLOC_CTX *mem_ctx, int argc,
					  const char **argv)
{
	struct policy_handle pol;
	NTSTATUS status, result;
	struct dom_sid *sids = NULL;
	char **domains = NULL;
	char **names = NULL;
	enum lsa_SidType *types = NULL;
	int i, level;
	struct dcerpc_binding_handle *b = cli->binding_handle;

	if (argc < 3) {
		printf("Usage: %s [level] [sid1 [sid2 [...]]]\n", argv[0]);
		return NT_STATUS_OK;
	}

	status = rpccli_lsa_open_policy(cli, mem_ctx, True,
					LSA_POLICY_LOOKUP_NAMES,
					&pol);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}

	level = atoi(argv[1]);

	/* Convert arguments to sids */

	sids = talloc_array(mem_ctx, struct dom_sid, argc - 2);
	if (sids == NULL) {
		printf("could not allocate memory for %d sids\n", argc - 2);
		goto done;
	}

	for (i = 0; i < argc - 2; i++) {
		if (!string_to_sid(&sids[i], argv[i + 2])) {
			status = NT_STATUS_INVALID_SID;
			goto done;
		}
	}

	/* Lookup the SIDs */

	status = dcerpc_lsa_lookup_sids_generic(cli->binding_handle,
						mem_ctx,
						&pol,
						argc - 2,
						sids,
						level,
						&domains,
						&names,
						&types,
						false,
						&result);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}
	status = result;

	if (!NT_STATUS_IS_OK(status) && NT_STATUS_V(status) !=
	    NT_STATUS_V(STATUS_SOME_UNMAPPED))
	{
		goto done;
	}

	status = NT_STATUS_OK;

	/* Print results */

	for (i = 0; i < (argc - 2); i++) {
		struct dom_sid_buf sid_str;

		dom_sid_str_buf(&sids[i], &sid_str);
		if (types[i] == SID_NAME_DOMAIN) {
			printf("%s %s (%d)\n", sid_str.buf,
			       domains[i] ? domains[i] : "*unknown*",
			       types[i]);
		} else {
			printf("%s %s\\%s (%d)\n", sid_str.buf,
			       domains[i] ? domains[i] : "*unknown*",
			       names[i] ? names[i] : "*unknown*",
			       types[i]);
		}
	}

	dcerpc_lsa_Close(b, mem_ctx, &pol, &result);

 done:
	return status;
}

/* Resolve a list of SIDs to a list of names */

static NTSTATUS cmd_lsa_lookup_sids3(struct rpc_pipe_client *cli,
				     TALLOC_CTX *mem_ctx,
				     int argc, const char **argv)
{
	NTSTATUS status = NT_STATUS_UNSUCCESSFUL, result;
	int i;
	struct lsa_SidArray sids;
	struct lsa_RefDomainList *domains = NULL;
	struct lsa_TransNameArray2 names;
	uint32_t count = 0;
	struct dcerpc_binding_handle *b = cli->binding_handle;

	if (argc == 1) {
		printf("Usage: %s [sid1 [sid2 [...]]]\n", argv[0]);
		return NT_STATUS_OK;
	}

	ZERO_STRUCT(names);

	/* Convert arguments to sids */

	sids.num_sids = argc-1;
	sids.sids = talloc_array(mem_ctx, struct lsa_SidPtr, sids.num_sids);
	if (!sids.sids) {
		printf("could not allocate memory for %d sids\n", sids.num_sids);
		goto done;
	}

	for (i = 0; i < sids.num_sids; i++) {
		sids.sids[i].sid = talloc(sids.sids, struct dom_sid);
		if (sids.sids[i].sid == NULL) {
			status = NT_STATUS_NO_MEMORY;
			goto done;
		}
		if (!string_to_sid(sids.sids[i].sid, argv[i+1])) {
			status = NT_STATUS_INVALID_SID;
			goto done;
		}
	}

	/* Lookup the SIDs */
	status = dcerpc_lsa_LookupSids3(b, mem_ctx,
					&sids,
					&domains,
					&names,
					1,
					&count,
					0,
					0,
					&result);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}
	if (!NT_STATUS_IS_OK(result) && NT_STATUS_V(result) !=
	    NT_STATUS_V(STATUS_SOME_UNMAPPED)) {
		status = result;
		goto done;
	}

	status = NT_STATUS_OK;

	/* Print results */

	for (i = 0; i < names.count; i++) {
		struct dom_sid_buf sid_str;

		if (i >= sids.num_sids) {
			break;
		}
		printf("%s %s (%d)\n",
		       dom_sid_str_buf(sids.sids[i].sid, &sid_str),
		       names.names[i].name.string,
		       names.names[i].sid_type);
	}

 done:
	return status;
}


/* Enumerate list of trusted domains */

static NTSTATUS cmd_lsa_enum_trust_dom(struct rpc_pipe_client *cli,
				       TALLOC_CTX *mem_ctx,
				       int argc,
				       const char **argv)
{
	struct policy_handle pol;
	NTSTATUS status, result;
	struct lsa_DomainList domain_list;
	struct dcerpc_binding_handle *b = cli->binding_handle;

	/* defaults, but may be changed using params */
	uint32_t enum_ctx = 0;
	int i;
	uint32_t max_size = (uint32_t)-1;

	if (argc > 2) {
		printf("Usage: %s [enum context (0)]\n", argv[0]);
		return NT_STATUS_OK;
	}

	if (argc == 2 && argv[1]) {
		enum_ctx = atoi(argv[2]);
	}

	status = rpccli_lsa_open_policy(cli, mem_ctx, True,
				     LSA_POLICY_VIEW_LOCAL_INFORMATION,
				     &pol);

	if (!NT_STATUS_IS_OK(status))
		goto done;

	status = STATUS_MORE_ENTRIES;

	while (NT_STATUS_EQUAL(status, STATUS_MORE_ENTRIES)) {

		/* Lookup list of trusted domains */

		status = dcerpc_lsa_EnumTrustDom(b, mem_ctx,
						 &pol,
						 &enum_ctx,
						 &domain_list,
						 max_size,
						 &result);
		if (!NT_STATUS_IS_OK(status)) {
			goto done;
		}
		if (!NT_STATUS_IS_OK(result) &&
		    !NT_STATUS_EQUAL(result, NT_STATUS_NO_MORE_ENTRIES) &&
		    !NT_STATUS_EQUAL(result, STATUS_MORE_ENTRIES)) {
			status = result;
			goto done;
		}

		/* Print results: list of names and sids returned in this
		 * response. */
		for (i = 0; i < domain_list.count; i++) {
			struct dom_sid_buf sid_str;

			printf("%s %s\n",
				domain_list.domains[i].name.string ?
				domain_list.domains[i].name.string : "*unknown*",
				dom_sid_str_buf(domain_list.domains[i].sid,
						&sid_str));
		}
	}

	dcerpc_lsa_Close(b, mem_ctx, &pol, &result);
 done:
	return status;
}

/* Enumerates privileges */

static NTSTATUS cmd_lsa_enum_privilege(struct rpc_pipe_client *cli,
				       TALLOC_CTX *mem_ctx,
				       int argc,
				       const char **argv)
{
	struct policy_handle pol;
	NTSTATUS status, result;
	struct lsa_PrivArray priv_array;
	struct dcerpc_binding_handle *b = cli->binding_handle;

	uint32_t enum_context=0;
	uint32_t pref_max_length=0x1000;
	int i;

	if (argc > 3) {
		printf("Usage: %s [enum context] [max length]\n", argv[0]);
		return NT_STATUS_OK;
	}

	if (argc>=2)
		enum_context=atoi(argv[1]);

	if (argc==3)
		pref_max_length=atoi(argv[2]);

	status = rpccli_lsa_open_policy(cli, mem_ctx, True,
				     SEC_FLAG_MAXIMUM_ALLOWED,
				     &pol);

	if (!NT_STATUS_IS_OK(status))
		goto done;

	status = dcerpc_lsa_EnumPrivs(b, mem_ctx,
				      &pol,
				      &enum_context,
				      &priv_array,
				      pref_max_length,
				      &result);
	if (!NT_STATUS_IS_OK(status))
		goto done;
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

	/* Print results */
	printf("found %d privileges\n\n", priv_array.count);

	for (i = 0; i < priv_array.count; i++) {
		printf("%s \t\t%d:%d (0x%x:0x%x)\n",
		       priv_array.privs[i].name.string ? priv_array.privs[i].name.string : "*unknown*",
		       priv_array.privs[i].luid.high,
		       priv_array.privs[i].luid.low,
		       priv_array.privs[i].luid.high,
		       priv_array.privs[i].luid.low);
	}

	dcerpc_lsa_Close(b, mem_ctx, &pol, &result);
 done:
	return status;
}

/* Get privilege name */

static NTSTATUS cmd_lsa_get_dispname(struct rpc_pipe_client *cli,
				     TALLOC_CTX *mem_ctx,
				     int argc,
				     const char **argv)
{
	struct policy_handle pol;
	NTSTATUS status, result;
	struct dcerpc_binding_handle *b = cli->binding_handle;

	uint16_t lang_id=0;
	uint16_t lang_id_sys=0;
	uint16_t lang_id_desc;
	struct lsa_String lsa_name;
	struct lsa_StringLarge *description = NULL;

	if (argc != 2) {
		printf("Usage: %s privilege name\n", argv[0]);
		return NT_STATUS_OK;
	}

	status = rpccli_lsa_open_policy(cli, mem_ctx, True,
				     SEC_FLAG_MAXIMUM_ALLOWED,
				     &pol);

	if (!NT_STATUS_IS_OK(status))
		goto done;

	init_lsa_String(&lsa_name, argv[1]);

	status = dcerpc_lsa_LookupPrivDisplayName(b, mem_ctx,
						  &pol,
						  &lsa_name,
						  lang_id,
						  lang_id_sys,
						  &description,
						  &lang_id_desc,
						  &result);
	if (!NT_STATUS_IS_OK(status))
		goto done;
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

	/* Print results */
	printf("%s -> %s (language: 0x%x)\n", argv[1], description->string, lang_id_desc);

	dcerpc_lsa_Close(b, mem_ctx, &pol, &result);
 done:
	return status;
}

/* Enumerate the LSA SIDS */

static NTSTATUS cmd_lsa_enum_sids(struct rpc_pipe_client *cli,
				  TALLOC_CTX *mem_ctx,
				  int argc,
				  const char **argv)
{
	struct policy_handle pol;
	NTSTATUS status, result;
	struct dcerpc_binding_handle *b = cli->binding_handle;

	uint32_t enum_context=0;
	uint32_t pref_max_length=0x1000;
	struct lsa_SidArray sid_array;
	int i;

	if (argc > 3) {
		printf("Usage: %s [enum context] [max length]\n", argv[0]);
		return NT_STATUS_OK;
	}

	if (argc>=2)
		enum_context=atoi(argv[1]);

	if (argc==3)
		pref_max_length=atoi(argv[2]);

	status = rpccli_lsa_open_policy(cli, mem_ctx, True,
				     SEC_FLAG_MAXIMUM_ALLOWED,
				     &pol);

	if (!NT_STATUS_IS_OK(status))
		goto done;

	status = dcerpc_lsa_EnumAccounts(b, mem_ctx,
					 &pol,
					 &enum_context,
					 &sid_array,
					 pref_max_length,
					 &result);
	if (!NT_STATUS_IS_OK(status))
		goto done;
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

	/* Print results */
	printf("found %d SIDs\n\n", sid_array.num_sids);

	for (i = 0; i < sid_array.num_sids; i++) {
		struct dom_sid_buf sid_str;

		printf("%s\n",
		       dom_sid_str_buf(sid_array.sids[i].sid, &sid_str));
	}

	dcerpc_lsa_Close(b, mem_ctx, &pol, &result);
 done:
	return status;
}

/* Create a new account */

static NTSTATUS cmd_lsa_create_account(struct rpc_pipe_client *cli,
				       TALLOC_CTX *mem_ctx,
				       int argc,
				       const char **argv)
{
	struct policy_handle dom_pol;
	struct policy_handle user_pol;
	NTSTATUS status, result;
	uint32_t des_access = 0x000f000f;
	struct dcerpc_binding_handle *b = cli->binding_handle;
	union lsa_revision_info out_revision_info = {
		.info1 = {
			.revision = 0,
		},
	};
	uint32_t out_version = 0;

	struct dom_sid sid;

	if (argc != 2 ) {
		printf("Usage: %s SID\n", argv[0]);
		return NT_STATUS_OK;
	}

	status = name_to_sid(cli, mem_ctx, &sid, argv[1]);
	if (!NT_STATUS_IS_OK(status))
		goto done;

	status = dcerpc_lsa_open_policy_fallback(cli,
						 mem_ctx,
						 cli->srv_name_slash,
						 true,
						 SEC_FLAG_MAXIMUM_ALLOWED,
						 &out_version,
						 &out_revision_info,
						 &dom_pol,
						 &result);
	if (any_nt_status_not_ok(status, result, &status)) {
		goto done;
	}

	status = dcerpc_lsa_CreateAccount(b, mem_ctx,
					  &dom_pol,
					  &sid,
					  des_access,
					  &user_pol,
					  &result);
	if (!NT_STATUS_IS_OK(status))
		goto done;
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

	printf("Account for SID %s successfully created\n\n", argv[1]);
	status = NT_STATUS_OK;

	dcerpc_lsa_Close(b, mem_ctx, &dom_pol, &result);
 done:
	return status;
}


/* Enumerate the privileges of an SID */

static NTSTATUS cmd_lsa_enum_privsaccounts(struct rpc_pipe_client *cli,
					   TALLOC_CTX *mem_ctx,
					   int argc,
					   const char **argv)
{
	struct policy_handle dom_pol;
	struct policy_handle user_pol;
	NTSTATUS status, result;
	uint32_t access_desired = 0x000f000f;
	struct dom_sid sid;
	struct lsa_PrivilegeSet *privs = NULL;
	int i;
	struct dcerpc_binding_handle *b = cli->binding_handle;
	union lsa_revision_info out_revision_info = {
		.info1 = {
			.revision = 0,
		},
	};
	uint32_t out_version = 0;

	if (argc != 2 ) {
		printf("Usage: %s SID\n", argv[0]);
		return NT_STATUS_OK;
	}

	status = name_to_sid(cli, mem_ctx, &sid, argv[1]);
	if (!NT_STATUS_IS_OK(status))
		goto done;

	status = dcerpc_lsa_open_policy_fallback(cli,
						 mem_ctx,
						 cli->srv_name_slash,
						 true,
						 SEC_FLAG_MAXIMUM_ALLOWED,
						 &out_version,
						 &out_revision_info,
						 &dom_pol,
						 &result);
	if (any_nt_status_not_ok(status, result, &status)) {
		goto done;
	}

	status = dcerpc_lsa_OpenAccount(b, mem_ctx,
					&dom_pol,
					&sid,
					access_desired,
					&user_pol,
					&result);
	if (!NT_STATUS_IS_OK(status))
		goto done;
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

	status = dcerpc_lsa_EnumPrivsAccount(b, mem_ctx,
					     &user_pol,
					     &privs,
					     &result);
	if (!NT_STATUS_IS_OK(status))
		goto done;
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

	/* Print results */
	printf("found %d privileges for SID %s\n\n", privs->count, argv[1]);
	printf("high\tlow\tattribute\n");

	for (i = 0; i < privs->count; i++) {
		printf("%u\t%u\t%u\n",
			privs->set[i].luid.high,
			privs->set[i].luid.low,
			privs->set[i].attribute);
	}

	dcerpc_lsa_Close(b, mem_ctx, &dom_pol, &result);
 done:
	return status;
}


/* Enumerate the privileges of an SID via LsaEnumerateAccountRights */

static NTSTATUS cmd_lsa_enum_acct_rights(struct rpc_pipe_client *cli,
					 TALLOC_CTX *mem_ctx,
					 int argc,
					 const char **argv)
{
	struct policy_handle dom_pol;
	NTSTATUS status, result;
	struct dom_sid sid;
	struct dom_sid_buf buf;
	struct lsa_RightSet rights;
	struct dcerpc_binding_handle *b = cli->binding_handle;
	union lsa_revision_info out_revision_info = {
		.info1 = {
			.revision = 0,
		},
	};
	uint32_t out_version = 0;

	int i;

	if (argc != 2 ) {
		printf("Usage: %s SID\n", argv[0]);
		return NT_STATUS_OK;
	}

	status = name_to_sid(cli, mem_ctx, &sid, argv[1]);
	if (!NT_STATUS_IS_OK(status))
		goto done;

	status = dcerpc_lsa_open_policy_fallback(cli,
						 mem_ctx,
						 cli->srv_name_slash,
						 true,
						 SEC_FLAG_MAXIMUM_ALLOWED,
						 &out_version,
						 &out_revision_info,
						 &dom_pol,
						 &result);
	if (any_nt_status_not_ok(status, result, &status)) {
		goto done;
	}

	status = dcerpc_lsa_EnumAccountRights(b, mem_ctx,
					      &dom_pol,
					      &sid,
					      &rights,
					      &result);
	if (!NT_STATUS_IS_OK(status))
		goto done;
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

	printf("found %d privileges for SID %s\n", rights.count,
	       dom_sid_str_buf(&sid, &buf));

	for (i = 0; i < rights.count; i++) {
		printf("\t%s\n", rights.names[i].string);
	}

	dcerpc_lsa_Close(b, mem_ctx, &dom_pol, &result);
 done:
	return status;
}


/* add some privileges to a SID via LsaAddAccountRights */

static NTSTATUS cmd_lsa_add_acct_rights(struct rpc_pipe_client *cli,
					TALLOC_CTX *mem_ctx,
					int argc,
					const char **argv)
{
	struct policy_handle dom_pol;
	NTSTATUS status, result;
	struct lsa_RightSet rights;
	struct dom_sid sid;
	int i;
	struct dcerpc_binding_handle *b = cli->binding_handle;
	union lsa_revision_info out_revision_info = {
		.info1 = {
			.revision = 0,
		},
	};
	uint32_t out_version = 0;

	if (argc < 3 ) {
		printf("Usage: %s SID [rights...]\n", argv[0]);
		return NT_STATUS_OK;
	}

	status = name_to_sid(cli, mem_ctx, &sid, argv[1]);
	if (!NT_STATUS_IS_OK(status))
		goto done;

	status = dcerpc_lsa_open_policy_fallback(cli,
						 mem_ctx,
						 cli->srv_name_slash,
						 true,
						 SEC_FLAG_MAXIMUM_ALLOWED,
						 &out_version,
						 &out_revision_info,
						 &dom_pol,
						 &result);
	if (any_nt_status_not_ok(status, result, &status)) {
		goto done;
	}

	rights.count = argc-2;
	rights.names = talloc_array(mem_ctx, struct lsa_StringLarge,
				    rights.count);
	if (!rights.names) {
		return NT_STATUS_NO_MEMORY;
	}

	for (i=0; i<argc-2; i++) {
		init_lsa_StringLarge(&rights.names[i], argv[i+2]);
	}

	status = dcerpc_lsa_AddAccountRights(b, mem_ctx,
					     &dom_pol,
					     &sid,
					     &rights,
					     &result);
	if (!NT_STATUS_IS_OK(status))
		goto done;
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

	dcerpc_lsa_Close(b, mem_ctx, &dom_pol, &result);
 done:
	return status;
}


/* remove some privileges to a SID via LsaRemoveAccountRights */

static NTSTATUS cmd_lsa_remove_acct_rights(struct rpc_pipe_client *cli,
					   TALLOC_CTX *mem_ctx,
					   int argc,
					   const char **argv)
{
	struct policy_handle dom_pol;
	NTSTATUS status, result;
	struct lsa_RightSet rights;
	struct dom_sid sid;
	int i;
	struct dcerpc_binding_handle *b = cli->binding_handle;
	union lsa_revision_info out_revision_info = {
		.info1 = {
			.revision = 0,
		},
	};
	uint32_t out_version = 0;

	if (argc < 3 ) {
		printf("Usage: %s SID [rights...]\n", argv[0]);
		return NT_STATUS_OK;
	}

	status = name_to_sid(cli, mem_ctx, &sid, argv[1]);
	if (!NT_STATUS_IS_OK(status))
		goto done;

	status = dcerpc_lsa_open_policy_fallback(cli,
						 mem_ctx,
						 cli->srv_name_slash,
						 true,
						 SEC_FLAG_MAXIMUM_ALLOWED,
						 &out_version,
						 &out_revision_info,
						 &dom_pol,
						 &result);
	if (any_nt_status_not_ok(status, result, &status)) {
		goto done;
	}

	rights.count = argc-2;
	rights.names = talloc_array(mem_ctx, struct lsa_StringLarge,
				    rights.count);
	if (!rights.names) {
		return NT_STATUS_NO_MEMORY;
	}

	for (i=0; i<argc-2; i++) {
		init_lsa_StringLarge(&rights.names[i], argv[i+2]);
	}

	status = dcerpc_lsa_RemoveAccountRights(b, mem_ctx,
						&dom_pol,
						&sid,
						false,
						&rights,
						&result);
	if (!NT_STATUS_IS_OK(status))
		goto done;
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

	dcerpc_lsa_Close(b, mem_ctx, &dom_pol, &result);

 done:
	return status;
}


/* Get a privilege value given its name */

static NTSTATUS cmd_lsa_lookup_priv_value(struct rpc_pipe_client *cli,
					  TALLOC_CTX *mem_ctx,
					  int argc,
					  const char **argv)
{
	struct policy_handle pol;
	NTSTATUS status, result;
	struct lsa_LUID luid;
	struct lsa_String name;
	struct dcerpc_binding_handle *b = cli->binding_handle;
	union lsa_revision_info out_revision_info = {
		.info1 = {
			.revision = 0,
		},
	};
	uint32_t out_version = 0;

	if (argc != 2 ) {
		printf("Usage: %s name\n", argv[0]);
		return NT_STATUS_OK;
	}

	status = dcerpc_lsa_open_policy_fallback(cli,
						 mem_ctx,
						 cli->srv_name_slash,
						 true,
						 SEC_FLAG_MAXIMUM_ALLOWED,
						 &out_version,
						 &out_revision_info,
						 &pol,
						 &result);
	if (any_nt_status_not_ok(status, result, &status)) {
		goto done;
	}

	init_lsa_String(&name, argv[1]);

	status = dcerpc_lsa_LookupPrivValue(b, mem_ctx,
					    &pol,
					    &name,
					    &luid,
					    &result);
	if (!NT_STATUS_IS_OK(status))
		goto done;
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

	/* Print results */

	printf("%u:%u (0x%x:0x%x)\n", luid.high, luid.low, luid.high, luid.low);

	dcerpc_lsa_Close(b, mem_ctx, &pol, &result);
 done:
	return status;
}

/* Query LSA security object */

static NTSTATUS cmd_lsa_query_secobj(struct rpc_pipe_client *cli,
				     TALLOC_CTX *mem_ctx,
				     int argc,
				     const char **argv)
{
	struct policy_handle pol;
	NTSTATUS status, result;
	struct sec_desc_buf *sdb;
	uint32_t sec_info = SECINFO_DACL;
	struct dcerpc_binding_handle *b = cli->binding_handle;
	union lsa_revision_info out_revision_info = {
		.info1 = {
			.revision = 0,
		},
	};
	uint32_t out_version = 0;

	if (argc < 1 || argc > 2) {
		printf("Usage: %s [sec_info]\n", argv[0]);
		return NT_STATUS_OK;
	}

	if (argc == 2)
		sscanf(argv[1], "%x", &sec_info);

	status = dcerpc_lsa_open_policy_fallback(cli,
						 mem_ctx,
						 cli->srv_name_slash,
						 true,
						 SEC_FLAG_MAXIMUM_ALLOWED,
						 &out_version,
						 &out_revision_info,
						 &pol,
						 &result);
	if (any_nt_status_not_ok(status, result, &status)) {
		goto done;
	}

	status = dcerpc_lsa_QuerySecurity(b, mem_ctx,
					  &pol,
					  sec_info,
					  &sdb,
					  &result);
	if (!NT_STATUS_IS_OK(status))
		goto done;
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

	/* Print results */

	display_sec_desc(sdb->sd);

	dcerpc_lsa_Close(b, mem_ctx, &pol, &result);
 done:
	return status;
}

static void display_trust_dom_info_4(struct lsa_TrustDomainInfoPassword *p,
				     DATA_BLOB session_key)
{
	char *pwd, *pwd_old;

	DATA_BLOB data 	   = data_blob_const(p->password->data, p->password->length);
	DATA_BLOB data_old = data_blob_const(p->old_password->data, p->old_password->length);

	pwd 	= sess_decrypt_string(talloc_tos(), &data, &session_key);
	pwd_old = sess_decrypt_string(talloc_tos(), &data_old, &session_key);

	d_printf("Password:\t%s\n", pwd);
	d_printf("Old Password:\t%s\n", pwd_old);

	talloc_free(pwd);
	talloc_free(pwd_old);
}

static void display_trust_dom_info(TALLOC_CTX *mem_ctx,
				   union lsa_TrustedDomainInfo *info,
				   enum lsa_TrustDomInfoEnum info_class,
				   DATA_BLOB session_key)
{
	switch (info_class) {
		case LSA_TRUSTED_DOMAIN_INFO_PASSWORD:
			display_trust_dom_info_4(&info->password, session_key);
			break;
		default: {
			const char *str = NULL;
			str = NDR_PRINT_UNION_STRING(mem_ctx,
						     lsa_TrustedDomainInfo,
						     info_class, info);
			if (str) {
				d_printf("%s\n", str);
			}
			break;
		}
	}
}

static NTSTATUS cmd_lsa_query_trustdominfobysid(struct rpc_pipe_client *cli,
						TALLOC_CTX *mem_ctx,
						int argc,
						const char **argv)
{
	struct policy_handle pol;
	NTSTATUS status, result;
	struct dom_sid dom_sid;
	uint32_t access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	union lsa_TrustedDomainInfo *info = NULL;
	enum lsa_TrustDomInfoEnum info_class = 1;
	DATA_BLOB session_key;
	struct dcerpc_binding_handle *b = cli->binding_handle;
	union lsa_revision_info out_revision_info = {
		.info1 = {
			.revision = 0,
		},
	};
	uint32_t out_version = 0;

	if (argc > 3 || argc < 2) {
		printf("Usage: %s [sid] [info_class]\n", argv[0]);
		return NT_STATUS_OK;
	}

	if (!string_to_sid(&dom_sid, argv[1]))
		return NT_STATUS_NO_MEMORY;

	if (argc == 3)
		info_class = atoi(argv[2]);

	status = dcerpc_lsa_open_policy_fallback(cli,
						 mem_ctx,
						 cli->srv_name_slash,
						 true,
						 access_mask,
						 &out_version,
						 &out_revision_info,
						 &pol,
						 &result);
	if (any_nt_status_not_ok(status, result, &status)) {
		goto done;
	}

	status = dcerpc_lsa_QueryTrustedDomainInfoBySid(b, mem_ctx,
							&pol,
							&dom_sid,
							info_class,
							&info,
							&result);
	if (!NT_STATUS_IS_OK(status))
		goto done;
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

	status = dcerpc_binding_handle_transport_session_key(
				b, mem_ctx, &session_key);
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(0, ("Could not retrieve session key: %s\n", nt_errstr(status)));
		goto done;
	}

	display_trust_dom_info(mem_ctx, info, info_class, session_key);

 done:
	dcerpc_lsa_Close(b, mem_ctx, &pol, &result);

	return status;
}

static NTSTATUS cmd_lsa_query_trustdominfobyname(struct rpc_pipe_client *cli,
						 TALLOC_CTX *mem_ctx,
						 int argc,
						 const char **argv)
{
	struct policy_handle pol;
	NTSTATUS status, result;
	uint32_t access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	union lsa_TrustedDomainInfo *info = NULL;
	enum lsa_TrustDomInfoEnum info_class = 1;
	struct lsa_String trusted_domain;
	struct dcerpc_binding_handle *b = cli->binding_handle;
	DATA_BLOB session_key;
	union lsa_revision_info out_revision_info = {
		.info1 = {
			.revision = 0,
		},
	};
	uint32_t out_version = 0;

	if (argc > 3 || argc < 2) {
		printf("Usage: %s [name] [info_class]\n", argv[0]);
		return NT_STATUS_OK;
	}

	if (argc == 3)
		info_class = atoi(argv[2]);

	status = dcerpc_lsa_open_policy_fallback(cli,
						 mem_ctx,
						 cli->srv_name_slash,
						 true,
						 access_mask,
						 &out_version,
						 &out_revision_info,
						 &pol,
						 &result);
	if (any_nt_status_not_ok(status, result, &status)) {
		goto done;
	}

	init_lsa_String(&trusted_domain, argv[1]);

	status = dcerpc_lsa_QueryTrustedDomainInfoByName(b, mem_ctx,
							 &pol,
							 &trusted_domain,
							 info_class,
							 &info,
							 &result);
	if (!NT_STATUS_IS_OK(status))
		goto done;
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

	status = dcerpc_binding_handle_transport_session_key(
				b, mem_ctx, &session_key);
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(0, ("Could not retrieve session key: %s\n", nt_errstr(status)));
		goto done;
	}

	display_trust_dom_info(mem_ctx, info, info_class, session_key);

 done:
	dcerpc_lsa_Close(b, mem_ctx, &pol, &result);

	return status;
}

static NTSTATUS cmd_lsa_set_trustdominfo(struct rpc_pipe_client *cli,
					 TALLOC_CTX *mem_ctx, int argc,
					 const char **argv)
{
	struct policy_handle pol, trustdom_pol;
	NTSTATUS status, result;
	uint32_t access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	union lsa_TrustedDomainInfo info;
	struct dom_sid dom_sid;
	enum lsa_TrustDomInfoEnum info_class = 1;
	struct dcerpc_binding_handle *b = cli->binding_handle;
	union lsa_revision_info out_revision_info = {
		.info1 = {
			.revision = 0,
		},
	};
	uint32_t out_version = 0;

	if (argc > 4 || argc < 3) {
		printf("Usage: %s [sid] [info_class] [value]\n", argv[0]);
		return NT_STATUS_OK;
	}

	if (!string_to_sid(&dom_sid, argv[1])) {
		return NT_STATUS_NO_MEMORY;
	}


	info_class = atoi(argv[2]);

	switch (info_class) {
	case 13: /* LSA_TRUSTED_DOMAIN_SUPPORTED_ENCRYPTION_TYPES */
		info.enc_types.enc_types = atoi(argv[3]);
		break;
	default:
		return NT_STATUS_INVALID_PARAMETER;
	}

	status = dcerpc_lsa_open_policy_fallback(cli,
						 mem_ctx,
						 cli->srv_name_slash,
						 true,
						 access_mask,
						 &out_version,
						 &out_revision_info,
						 &pol,
						 &result);
	if (any_nt_status_not_ok(status, result, &status)) {
		goto done;
	}

	status = dcerpc_lsa_OpenTrustedDomain(b, mem_ctx,
					      &pol,
					      &dom_sid,
					      access_mask,
					      &trustdom_pol,
					      &result);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

	status = dcerpc_lsa_SetInformationTrustedDomain(b, mem_ctx,
							&trustdom_pol,
							info_class,
							&info,
							&result);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}
 done:
	dcerpc_lsa_Close(b, mem_ctx, &trustdom_pol, &result);
	dcerpc_lsa_Close(b, mem_ctx, &pol, &result);

	return status;
}

static NTSTATUS cmd_lsa_query_trustdominfo(struct rpc_pipe_client *cli,
					   TALLOC_CTX *mem_ctx,
					   int argc,
					   const char **argv)
{
	struct policy_handle pol, trustdom_pol;
	NTSTATUS status, result;
	uint32_t access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	union lsa_TrustedDomainInfo *info = NULL;
	struct dom_sid dom_sid;
	enum lsa_TrustDomInfoEnum info_class = 1;
	DATA_BLOB session_key;
	struct dcerpc_binding_handle *b = cli->binding_handle;
	union lsa_revision_info out_revision_info = {
		.info1 = {
			.revision = 0,
		},
	};
	uint32_t out_version = 0;

	if (argc > 3 || argc < 2) {
		printf("Usage: %s [sid] [info_class]\n", argv[0]);
		return NT_STATUS_OK;
	}

	if (!string_to_sid(&dom_sid, argv[1]))
		return NT_STATUS_NO_MEMORY;


	if (argc == 3)
		info_class = atoi(argv[2]);

	status = dcerpc_lsa_open_policy_fallback(cli,
						 mem_ctx,
						 cli->srv_name_slash,
						 true,
						 access_mask,
						 &out_version,
						 &out_revision_info,
						 &pol,
						 &result);
	if (any_nt_status_not_ok(status, result, &status)) {
		goto done;
	}

	status = dcerpc_lsa_OpenTrustedDomain(b, mem_ctx,
					      &pol,
					      &dom_sid,
					      access_mask,
					      &trustdom_pol,
					      &result);
	if (!NT_STATUS_IS_OK(status))
		goto done;
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

	status = dcerpc_lsa_QueryTrustedDomainInfo(b, mem_ctx,
						   &trustdom_pol,
						   info_class,
						   &info,
						   &result);
	if (!NT_STATUS_IS_OK(status))
		goto done;
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

	status = dcerpc_binding_handle_transport_session_key(
				b, mem_ctx, &session_key);
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(0, ("Could not retrieve session key: %s\n", nt_errstr(status)));
		goto done;
	}

	display_trust_dom_info(mem_ctx, info, info_class, session_key);

 done:
	dcerpc_lsa_Close(b, mem_ctx, &pol, &result);

	return status;
}

static NTSTATUS cmd_lsa_get_username(struct rpc_pipe_client *cli,
                                     TALLOC_CTX *mem_ctx, int argc,
                                     const char **argv)
{
	NTSTATUS status, result;
	const char *servername = cli->desthost;
	struct lsa_String *account_name = NULL;
	struct lsa_String *authority_name = NULL;
	struct dcerpc_binding_handle *b = cli->binding_handle;

	if (argc > 2) {
		printf("Usage: %s servername\n", argv[0]);
		return NT_STATUS_OK;
	}

	status = dcerpc_lsa_GetUserName(b, mem_ctx,
					servername,
					&account_name,
					&authority_name,
					&result);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

	/* Print results */

	printf("Account Name: %s, Authority Name: %s\n",
		account_name->string, authority_name ? authority_name->string :
		"");

 done:
	return status;
}

static NTSTATUS cmd_lsa_add_priv(struct rpc_pipe_client *cli,
				 TALLOC_CTX *mem_ctx, int argc,
				 const char **argv)
{
	struct policy_handle dom_pol, user_pol;
	NTSTATUS status, result;
	struct lsa_PrivilegeSet privs;
	struct lsa_LUIDAttribute *set = NULL;
	struct dom_sid sid;
	int i;
	struct dcerpc_binding_handle *b = cli->binding_handle;
	union lsa_revision_info out_revision_info = {
		.info1 = {
			.revision = 0,
		},
	};
	uint32_t out_version = 0;

	ZERO_STRUCT(privs);

	if (argc < 3 ) {
		printf("Usage: %s SID [rights...]\n", argv[0]);
		return NT_STATUS_OK;
	}

	status = name_to_sid(cli, mem_ctx, &sid, argv[1]);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}

	status = dcerpc_lsa_open_policy_fallback(cli,
						 mem_ctx,
						 cli->srv_name_slash,
						 true,
						 SEC_FLAG_MAXIMUM_ALLOWED,
						 &out_version,
						 &out_revision_info,
						 &dom_pol,
						 &result);
	if (any_nt_status_not_ok(status, result, &status)) {
		goto done;
	}

	status = dcerpc_lsa_OpenAccount(b, mem_ctx,
					&dom_pol,
					&sid,
					SEC_FLAG_MAXIMUM_ALLOWED,
					&user_pol,
					&result);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

	for (i=2; i<argc; i++) {

		struct lsa_String priv_name;
		struct lsa_LUID luid;

		init_lsa_String(&priv_name, argv[i]);

		status = dcerpc_lsa_LookupPrivValue(b, mem_ctx,
						    &dom_pol,
						    &priv_name,
						    &luid,
						    &result);
		if (!NT_STATUS_IS_OK(status)) {
			continue;
		}
		if (!NT_STATUS_IS_OK(result)) {
			status = result;
			continue;
		}

		privs.count++;
		set = talloc_realloc(mem_ctx, set,
					   struct lsa_LUIDAttribute,
					   privs.count);
		if (!set) {
			return NT_STATUS_NO_MEMORY;
		}

		set[privs.count-1].luid = luid;
		set[privs.count-1].attribute = 0;
	}

	privs.set = set;

	status = dcerpc_lsa_AddPrivilegesToAccount(b, mem_ctx,
						   &user_pol,
						   &privs,
						   &result);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

	dcerpc_lsa_Close(b, mem_ctx, &user_pol, &result);
	dcerpc_lsa_Close(b, mem_ctx, &dom_pol, &result);
 done:
	return status;
}

static NTSTATUS cmd_lsa_del_priv(struct rpc_pipe_client *cli,
				 TALLOC_CTX *mem_ctx, int argc,
				 const char **argv)
{
	struct policy_handle dom_pol, user_pol;
	NTSTATUS status, result;
	struct lsa_PrivilegeSet privs;
	struct lsa_LUIDAttribute *set = NULL;
	struct dom_sid sid;
	int i;
	struct dcerpc_binding_handle *b = cli->binding_handle;
	union lsa_revision_info out_revision_info = {
		.info1 = {
			.revision = 0,
		},
	};
	uint32_t out_version = 0;

	ZERO_STRUCT(privs);

	if (argc < 3 ) {
		printf("Usage: %s SID [rights...]\n", argv[0]);
		return NT_STATUS_OK;
	}

	status = name_to_sid(cli, mem_ctx, &sid, argv[1]);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}

	status = dcerpc_lsa_open_policy_fallback(cli,
						 mem_ctx,
						 cli->srv_name_slash,
						 true,
						 SEC_FLAG_MAXIMUM_ALLOWED,
						 &out_version,
						 &out_revision_info,
						 &dom_pol,
						 &result);
	if (any_nt_status_not_ok(status, result, &status)) {
		goto done;
	}

	status = dcerpc_lsa_OpenAccount(b, mem_ctx,
					&dom_pol,
					&sid,
					SEC_FLAG_MAXIMUM_ALLOWED,
					&user_pol,
					&result);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

	for (i=2; i<argc; i++) {

		struct lsa_String priv_name;
		struct lsa_LUID luid;

		init_lsa_String(&priv_name, argv[i]);

		status = dcerpc_lsa_LookupPrivValue(b, mem_ctx,
						    &dom_pol,
						    &priv_name,
						    &luid,
						    &result);
		if (!NT_STATUS_IS_OK(status)) {
			continue;
		}
		if (!NT_STATUS_IS_OK(result)) {
			status = result;
			continue;
		}

		privs.count++;
		set = talloc_realloc(mem_ctx, set,
					   struct lsa_LUIDAttribute,
					   privs.count);
		if (!set) {
			return NT_STATUS_NO_MEMORY;
		}

		set[privs.count-1].luid = luid;
		set[privs.count-1].attribute = 0;
	}

	privs.set = set;


	status = dcerpc_lsa_RemovePrivilegesFromAccount(b, mem_ctx,
							&user_pol,
							false,
							&privs,
							&result);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

	dcerpc_lsa_Close(b, mem_ctx, &user_pol, &result);
	dcerpc_lsa_Close(b, mem_ctx, &dom_pol, &result);
 done:
	return status;
}

static NTSTATUS cmd_lsa_create_secret(struct rpc_pipe_client *cli,
				      TALLOC_CTX *mem_ctx, int argc,
				      const char **argv)
{
	NTSTATUS status, result;
	struct policy_handle handle, sec_handle;
	struct lsa_String name;
	struct dcerpc_binding_handle *b = cli->binding_handle;
	union lsa_revision_info out_revision_info = {
		.info1 = {
			.revision = 0,
		},
	};
	uint32_t out_version = 0;

	if (argc < 2) {
		printf("Usage: %s name\n", argv[0]);
		return NT_STATUS_OK;
	}

	status = dcerpc_lsa_open_policy_fallback(cli,
						 mem_ctx,
						 cli->srv_name_slash,
						 true,
						 SEC_FLAG_MAXIMUM_ALLOWED,
						 &out_version,
						 &out_revision_info,
						 &sec_handle,
						 &result);
	if (any_nt_status_not_ok(status, result, &status)) {
		return status;
	}

	init_lsa_String(&name, argv[1]);

	status = dcerpc_lsa_CreateSecret(b, mem_ctx,
					 &handle,
					 name,
					 SEC_FLAG_MAXIMUM_ALLOWED,
					 &sec_handle,
					 &result);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

 done:
	if (is_valid_policy_hnd(&sec_handle)) {
		dcerpc_lsa_Close(b, mem_ctx, &sec_handle, &result);
	}
	if (is_valid_policy_hnd(&handle)) {
		dcerpc_lsa_Close(b, mem_ctx, &handle, &result);
	}

	return status;
}

static NTSTATUS cmd_lsa_delete_secret(struct rpc_pipe_client *cli,
				      TALLOC_CTX *mem_ctx, int argc,
				      const char **argv)
{
	NTSTATUS status, result;
	struct policy_handle handle, sec_handle;
	struct lsa_String name;
	struct dcerpc_binding_handle *b = cli->binding_handle;
	union lsa_revision_info out_revision_info = {
		.info1 = {
			.revision = 0,
		},
	};
	uint32_t out_version = 0;

	if (argc < 2) {
		printf("Usage: %s name\n", argv[0]);
		return NT_STATUS_OK;
	}

	status = dcerpc_lsa_open_policy_fallback(cli,
						 mem_ctx,
						 cli->srv_name_slash,
						 true,
						 SEC_FLAG_MAXIMUM_ALLOWED,
						 &out_version,
						 &out_revision_info,
						 &handle,
						 &result);
	if (any_nt_status_not_ok(status, result, &status)) {
		return status;
	}

	init_lsa_String(&name, argv[1]);

	status = dcerpc_lsa_OpenSecret(b, mem_ctx,
				       &handle,
				       name,
				       SEC_FLAG_MAXIMUM_ALLOWED,
				       &sec_handle,
				       &result);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

	status = dcerpc_lsa_DeleteObject(b, mem_ctx,
					 &sec_handle,
					 &result);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

 done:
	if (is_valid_policy_hnd(&sec_handle)) {
		dcerpc_lsa_Close(b, mem_ctx, &sec_handle, &result);
	}
	if (is_valid_policy_hnd(&handle)) {
		dcerpc_lsa_Close(b, mem_ctx, &handle, &result);
	}

	return status;
}

static NTSTATUS cmd_lsa_query_secret(struct rpc_pipe_client *cli,
				     TALLOC_CTX *mem_ctx, int argc,
				     const char **argv)
{
	NTSTATUS status, result;
	struct policy_handle handle, sec_handle;
	struct lsa_String name;
	struct lsa_DATA_BUF_PTR new_val;
	NTTIME new_mtime = 0;
	struct lsa_DATA_BUF_PTR old_val;
	NTTIME old_mtime = 0;
	DATA_BLOB session_key;
	DATA_BLOB new_blob = data_blob_null;
	DATA_BLOB old_blob = data_blob_null;
	char *new_secret, *old_secret;
	struct dcerpc_binding_handle *b = cli->binding_handle;
	union lsa_revision_info out_revision_info = {
		.info1 = {
			.revision = 0,
		},
	};
	uint32_t out_version = 0;

	if (argc < 2) {
		printf("Usage: %s name\n", argv[0]);
		return NT_STATUS_OK;
	}

	status = dcerpc_lsa_open_policy_fallback(cli,
						 mem_ctx,
						 cli->srv_name_slash,
						 true,
						 SEC_FLAG_MAXIMUM_ALLOWED,
						 &out_version,
						 &out_revision_info,
						 &handle,
						 &result);
	if (any_nt_status_not_ok(status, result, &status)) {
		return status;
	}

	init_lsa_String(&name, argv[1]);

	status = dcerpc_lsa_OpenSecret(b, mem_ctx,
				       &handle,
				       name,
				       SEC_FLAG_MAXIMUM_ALLOWED,
				       &sec_handle,
				       &result);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

	ZERO_STRUCT(new_val);
	ZERO_STRUCT(old_val);

	status = dcerpc_lsa_QuerySecret(b, mem_ctx,
					&sec_handle,
					&new_val,
					&new_mtime,
					&old_val,
					&old_mtime,
					&result);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

	status = dcerpc_binding_handle_transport_session_key(
				b, mem_ctx, &session_key);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}

	if (new_val.buf) {
		new_blob = data_blob_const(new_val.buf->data, new_val.buf->length);
	}
	if (old_val.buf) {
		old_blob = data_blob_const(old_val.buf->data, old_val.buf->length);
	}

	new_secret = sess_decrypt_string(mem_ctx, &new_blob, &session_key);
	old_secret = sess_decrypt_string(mem_ctx, &old_blob, &session_key);
	if (new_secret) {
		d_printf("new secret: %s\n", new_secret);
	}
	if (old_secret) {
		d_printf("old secret: %s\n", old_secret);
	}

 done:
	if (is_valid_policy_hnd(&sec_handle)) {
		dcerpc_lsa_Close(b, mem_ctx, &sec_handle, &result);
	}
	if (is_valid_policy_hnd(&handle)) {
		dcerpc_lsa_Close(b, mem_ctx, &handle, &result);
	}

	return status;
}

static NTSTATUS cmd_lsa_set_secret(struct rpc_pipe_client *cli,
				   TALLOC_CTX *mem_ctx, int argc,
				   const char **argv)
{
	NTSTATUS status, result;
	struct policy_handle handle, sec_handle;
	struct lsa_String name;
	struct lsa_DATA_BUF new_val;
	struct lsa_DATA_BUF old_val;
	DATA_BLOB enc_key;
	DATA_BLOB session_key;
	struct dcerpc_binding_handle *b = cli->binding_handle;
	union lsa_revision_info out_revision_info = {
		.info1 = {
			.revision = 0,
		},
	};
	uint32_t out_version = 0;

	if (argc < 3) {
		printf("Usage: %s name secret\n", argv[0]);
		return NT_STATUS_OK;
	}

	status = dcerpc_lsa_open_policy_fallback(cli,
						 mem_ctx,
						 cli->srv_name_slash,
						 true,
						 SEC_FLAG_MAXIMUM_ALLOWED,
						 &out_version,
						 &out_revision_info,
						 &handle,
						 &result);
	if (any_nt_status_not_ok(status, result, &status)) {
		return status;
	}

	init_lsa_String(&name, argv[1]);

	status = dcerpc_lsa_OpenSecret(b, mem_ctx,
				       &handle,
				       name,
				       SEC_FLAG_MAXIMUM_ALLOWED,
				       &sec_handle,
				       &result);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

	ZERO_STRUCT(new_val);
	ZERO_STRUCT(old_val);

	status = dcerpc_binding_handle_transport_session_key(
				b, mem_ctx, &session_key);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}

	enc_key = sess_encrypt_string(argv[2], &session_key);

	new_val.length = enc_key.length;
	new_val.size = enc_key.length;
	new_val.data = enc_key.data;

	status = dcerpc_lsa_SetSecret(b, mem_ctx,
				      &sec_handle,
				      &new_val,
				      NULL,
				      &result);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

 done:
	if (is_valid_policy_hnd(&sec_handle)) {
		dcerpc_lsa_Close(b, mem_ctx, &sec_handle, &result);
	}
	if (is_valid_policy_hnd(&handle)) {
		dcerpc_lsa_Close(b, mem_ctx, &handle, &result);
	}

	return status;
}

static NTSTATUS cmd_lsa_retrieve_private_data(struct rpc_pipe_client *cli,
					      TALLOC_CTX *mem_ctx, int argc,
					      const char **argv)
{
	NTSTATUS status, result;
	struct policy_handle handle;
	struct lsa_String name;
	struct lsa_DATA_BUF *val;
	DATA_BLOB session_key;
	DATA_BLOB blob = data_blob_null;
	char *secret;
	struct dcerpc_binding_handle *b = cli->binding_handle;
	union lsa_revision_info out_revision_info = {
		.info1 = {
			.revision = 0,
		},
	};
	uint32_t out_version = 0;

	if (argc < 2) {
		printf("Usage: %s name\n", argv[0]);
		return NT_STATUS_OK;
	}

	status = dcerpc_lsa_open_policy_fallback(cli,
						 mem_ctx,
						 cli->srv_name_slash,
						 true,
						 SEC_FLAG_MAXIMUM_ALLOWED,
						 &out_version,
						 &out_revision_info,
						 &handle,
						 &result);
	if (any_nt_status_not_ok(status, result, &status)) {
		return status;
	}

	init_lsa_String(&name, argv[1]);

	ZERO_STRUCT(val);

	status = dcerpc_lsa_RetrievePrivateData(b, mem_ctx,
						&handle,
						&name,
						&val,
						&result);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

	status = dcerpc_binding_handle_transport_session_key(
				b, mem_ctx, &session_key);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}

	if (val) {
		blob = data_blob_const(val->data, val->length);
	}

	secret = sess_decrypt_string(mem_ctx, &blob, &session_key);
	if (secret) {
		d_printf("secret: %s\n", secret);
	}

 done:
	if (is_valid_policy_hnd(&handle)) {
		dcerpc_lsa_Close(b, mem_ctx, &handle, &result);
	}

	return status;
}

static NTSTATUS cmd_lsa_store_private_data(struct rpc_pipe_client *cli,
					   TALLOC_CTX *mem_ctx, int argc,
					   const char **argv)
{
	NTSTATUS status, result;
	struct policy_handle handle;
	struct lsa_String name;
	struct lsa_DATA_BUF val;
	DATA_BLOB session_key;
	DATA_BLOB enc_key;
	struct dcerpc_binding_handle *b = cli->binding_handle;
	union lsa_revision_info out_revision_info = {
		.info1 = {
			.revision = 0,
		},
	};
	uint32_t out_version = 0;

	if (argc < 3) {
		printf("Usage: %s name secret\n", argv[0]);
		return NT_STATUS_OK;
	}

	status = dcerpc_lsa_open_policy_fallback(cli,
						 mem_ctx,
						 cli->srv_name_slash,
						 true,
						 SEC_FLAG_MAXIMUM_ALLOWED,
						 &out_version,
						 &out_revision_info,
						 &handle,
						 &result);
	if (any_nt_status_not_ok(status, result, &status)) {
		return status;
	}

	init_lsa_String(&name, argv[1]);

	ZERO_STRUCT(val);

	status = dcerpc_binding_handle_transport_session_key(
				b, mem_ctx, &session_key);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}

	enc_key = sess_encrypt_string(argv[2], &session_key);

	val.length = enc_key.length;
	val.size = enc_key.length;
	val.data = enc_key.data;

	status = dcerpc_lsa_StorePrivateData(b, mem_ctx,
					     &handle,
					     &name,
					     &val,
					     &result);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

 done:
	if (is_valid_policy_hnd(&handle)) {
		dcerpc_lsa_Close(b, mem_ctx, &handle, &result);
	}

	return status;
}

static NTSTATUS cmd_lsa_create_trusted_domain(struct rpc_pipe_client *cli,
					      TALLOC_CTX *mem_ctx, int argc,
					      const char **argv)
{
	NTSTATUS status, result;
	struct policy_handle handle, trustdom_handle;
	struct dom_sid sid;
	struct lsa_DomainInfo info;
	struct dcerpc_binding_handle *b = cli->binding_handle;
	union lsa_revision_info out_revision_info = {
		.info1 = {
			.revision = 0,
		},
	};
	uint32_t out_version = 0;

	if (argc < 3) {
		printf("Usage: %s name sid\n", argv[0]);
		return NT_STATUS_OK;
	}

	status = dcerpc_lsa_open_policy_fallback(cli,
						 mem_ctx,
						 cli->srv_name_slash,
						 true,
						 SEC_FLAG_MAXIMUM_ALLOWED,
						 &out_version,
						 &out_revision_info,
						 &handle,
						 &result);
	if (any_nt_status_not_ok(status, result, &status)) {
		return status;
	}

	init_lsa_StringLarge(&info.name, argv[1]);
	info.sid = &sid;
	string_to_sid(&sid, argv[2]);

	status = dcerpc_lsa_CreateTrustedDomain(b, mem_ctx,
						&handle,
						&info,
						SEC_FLAG_MAXIMUM_ALLOWED,
						&trustdom_handle,
						&result);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

 done:
	if (is_valid_policy_hnd(&trustdom_handle)) {
		dcerpc_lsa_Close(b, mem_ctx, &trustdom_handle, &result);
	}

	if (is_valid_policy_hnd(&handle)) {
		dcerpc_lsa_Close(b, mem_ctx, &handle, &result);
	}

	return status;
}

static NTSTATUS cmd_lsa_create_trusted_domain_ex3(struct rpc_pipe_client *cli,
						  TALLOC_CTX *mem_ctx,
						  int argc,
						  const char **argv)
{
	struct dcerpc_binding_handle *b = cli->binding_handle;
	struct policy_handle handle = {
		.handle_type = 0,
	};
	struct policy_handle trustdom_handle = {
		.handle_type = 0,
	};
	struct dom_sid sid = {
		.num_auths = 0,
	};
	union lsa_revision_info out_revision_info = {
		.info1 = {
			.revision = 0,
		},
	};
	struct lsa_TrustDomainInfoAuthInfoInternalAES *authinfo_internal = NULL;
	struct lsa_TrustDomainInfoInfoEx trustinfo = {
		.trust_attributes = 0,
	};
	uint32_t out_version = 0;
	NTSTATUS status;
	NTSTATUS result = NT_STATUS_UNSUCCESSFUL;
	DATA_BLOB session_key = {
		.length = 0,
	};
	bool ok;

	if (argc < 7) {
		printf("Usage: %s trust_name trust_dns_name trust_sid "
		       "trust_directrion trust_type "
		       "incoming_trustpw outgoing_trustpw\n",
		       argv[0]);
		return NT_STATUS_OK;
	}

	status = dcerpc_binding_handle_transport_session_key(
				b, mem_ctx, &session_key);
	if (!NT_STATUS_IS_OK(status)) {
		DBG_ERR("Could not retrieve session key: %s\n",
			nt_errstr(status));
		goto done;
	}

	status = dcerpc_lsa_open_policy_fallback(cli,
						 mem_ctx,
						 cli->srv_name_slash,
						 true,
						 SEC_FLAG_MAXIMUM_ALLOWED,
						 &out_version,
						 &out_revision_info,
						 &handle,
						 &result);
	if (any_nt_status_not_ok(status, result, &status)) {
		DBG_ERR("Could not open LSA connection: %s\n",
			nt_errstr(status));
		return status;
	}

	init_lsa_StringLarge(&trustinfo.netbios_name, argv[1]);
	init_lsa_StringLarge(&trustinfo.domain_name, argv[2]);

	ok = string_to_sid(&sid, argv[3]);
	if (!ok) {
		status = NT_STATUS_INVALID_PARAMETER;
		DBG_ERR("Could not convert SID: %s\n", nt_errstr(status));
		goto done;
	}
	trustinfo.sid = &sid;

	trustinfo.trust_direction = atoi(argv[4]);
	trustinfo.trust_type = atoi(argv[5]);

	ok = rpc_lsa_encrypt_trustdom_info_aes(mem_ctx,
					       argv[6],
					       argv[6],
					       argv[7],
					       argv[7],
					       session_key,
					       &authinfo_internal);
	if (!ok) {
		status = NT_STATUS_INVALID_PARAMETER;
		DBG_ERR("Could not encrypt trust information: %s\n",
			nt_errstr(status));
		goto done;
	}

	status = dcerpc_lsa_CreateTrustedDomainEx3(b,
						   mem_ctx,
						   &handle,
						   &trustinfo,
						   authinfo_internal,
						   SEC_FLAG_MAXIMUM_ALLOWED,
						   &trustdom_handle,
						   &result);
	if (any_nt_status_not_ok(status, result, &status)) {
		goto done;
	}

done:
	if (is_valid_policy_hnd(&trustdom_handle)) {
		dcerpc_lsa_Close(b, mem_ctx, &trustdom_handle, &result);
	}

	if (is_valid_policy_hnd(&handle)) {
		dcerpc_lsa_Close(b, mem_ctx, &handle, &result);
	}

	return status;
}

static NTSTATUS cmd_lsa_create_trusted_domain_ex2(struct rpc_pipe_client *cli,
						  TALLOC_CTX *mem_ctx,
						  int argc,
						  const char **argv)
{
	struct dcerpc_binding_handle *b = cli->binding_handle;
	struct policy_handle handle = {
		.handle_type = 0,
	};
	struct policy_handle trustdom_handle = {
		.handle_type = 0,
	};
	struct dom_sid sid = {
		.num_auths = 0,
	};
	union lsa_revision_info out_revision_info = {
		.info1 = {
			.revision = 0,
		},
	};
	struct lsa_TrustDomainInfoAuthInfoInternal *authinfo_internal = NULL;
	struct lsa_TrustDomainInfoInfoEx trustinfo = {
		.trust_attributes = LSA_TRUST_ATTRIBUTE_USES_RC4_ENCRYPTION,
	};
	uint32_t out_version = 0;
	NTSTATUS status;
	NTSTATUS result = NT_STATUS_UNSUCCESSFUL;
	DATA_BLOB session_key = {
		.length = 0,
	};
	bool ok;

	if (argc < 7) {
		printf("Usage: %s trust_name trust_dns_name trust_sid "
		       "trust_directrion trust_type "
		       "incoming_trustpw outgoing_trustpw\n",
		       argv[0]);
		return NT_STATUS_OK;
	}

	status = dcerpc_binding_handle_transport_session_key(
				b, mem_ctx, &session_key);
	if (!NT_STATUS_IS_OK(status)) {
		DBG_ERR("Could not retrieve session key: %s\n",
			nt_errstr(status));
		goto done;
	}

	status = dcerpc_lsa_open_policy_fallback(cli,
						 mem_ctx,
						 cli->srv_name_slash,
						 true,
						 SEC_FLAG_MAXIMUM_ALLOWED,
						 &out_version,
						 &out_revision_info,
						 &handle,
						 &result);
	if (any_nt_status_not_ok(status, result, &status)) {
		DBG_ERR("Could not open LSA connection: %s\n",
			nt_errstr(status));
		return status;
	}

	init_lsa_StringLarge(&trustinfo.netbios_name, argv[1]);
	init_lsa_StringLarge(&trustinfo.domain_name, argv[2]);

	ok = string_to_sid(&sid, argv[3]);
	if (!ok) {
		status = NT_STATUS_INVALID_PARAMETER;
		DBG_ERR("Could not convert SID: %s\n", nt_errstr(status));
		goto done;
	}
	trustinfo.sid = &sid;

	trustinfo.trust_direction = atoi(argv[4]);
	trustinfo.trust_type = atoi(argv[5]);

	ok = rpc_lsa_encrypt_trustdom_info(mem_ctx,
					   argv[6],
					   argv[6],
					   argv[7],
					   argv[7],
					   session_key,
					   &authinfo_internal);
	if (!ok) {
		status = NT_STATUS_INVALID_PARAMETER;
		DBG_ERR("Could not encrypt trust information: %s\n",
			nt_errstr(status));
		goto done;
	}

	status = dcerpc_lsa_CreateTrustedDomainEx2(b,
						   mem_ctx,
						   &handle,
						   &trustinfo,
						   authinfo_internal,
						   SEC_FLAG_MAXIMUM_ALLOWED,
						   &trustdom_handle,
						   &result);
	if (any_nt_status_not_ok(status, result, &status)) {
		goto done;
	}

done:
	if (is_valid_policy_hnd(&trustdom_handle)) {
		dcerpc_lsa_Close(b, mem_ctx, &trustdom_handle, &result);
	}

	if (is_valid_policy_hnd(&handle)) {
		dcerpc_lsa_Close(b, mem_ctx, &handle, &result);
	}

	return status;
}

static NTSTATUS cmd_lsa_delete_trusted_domain(struct rpc_pipe_client *cli,
					      TALLOC_CTX *mem_ctx, int argc,
					      const char **argv)
{
	NTSTATUS status, result;
	struct policy_handle handle, trustdom_handle;
	struct lsa_String name;
	struct dom_sid *sid = NULL;
	struct dcerpc_binding_handle *b = cli->binding_handle;
	union lsa_revision_info out_revision_info = {
		.info1 = {
			.revision = 0,
		},
	};
	uint32_t out_version = 0;

	if (argc < 2) {
		printf("Usage: %s name\n", argv[0]);
		return NT_STATUS_OK;
	}

	status = dcerpc_lsa_open_policy_fallback(cli,
						 mem_ctx,
						 cli->srv_name_slash,
						 true,
						 SEC_FLAG_MAXIMUM_ALLOWED,
						 &out_version,
						 &out_revision_info,
						 &handle,
						 &result);
	if (any_nt_status_not_ok(status, result, &status)) {
		return status;
	}

	init_lsa_String(&name, argv[1]);

	status = dcerpc_lsa_OpenTrustedDomainByName(b, mem_ctx,
						    &handle,
						    name,
						    SEC_FLAG_MAXIMUM_ALLOWED,
						    &trustdom_handle,
						    &result);
	if (NT_STATUS_IS_OK(status) && NT_STATUS_IS_OK(result)) {
		goto delete_object;
	}

	{
		uint32_t resume_handle = 0;
		struct lsa_DomainList domains;
		int i;

		status = dcerpc_lsa_EnumTrustDom(b, mem_ctx,
						 &handle,
						 &resume_handle,
						 &domains,
						 0xffff,
						 &result);
		if (!NT_STATUS_IS_OK(status)) {
			goto done;
		}
		if (!NT_STATUS_IS_OK(result)) {
			status = result;
			goto done;
		}

		for (i=0; i < domains.count; i++) {
			if (strequal(domains.domains[i].name.string, argv[1])) {
				sid = domains.domains[i].sid;
				break;
			}
		}

		if (!sid) {
			return NT_STATUS_INVALID_SID;
		}
	}

	status = dcerpc_lsa_OpenTrustedDomain(b, mem_ctx,
					      &handle,
					      sid,
					      SEC_FLAG_MAXIMUM_ALLOWED,
					      &trustdom_handle,
					      &result);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

 delete_object:
	status = dcerpc_lsa_DeleteObject(b, mem_ctx,
					 &trustdom_handle,
					 &result);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}
	if (!NT_STATUS_IS_OK(result)) {
		status = result;
		goto done;
	}

 done:
	if (is_valid_policy_hnd(&trustdom_handle)) {
		dcerpc_lsa_Close(b, mem_ctx, &trustdom_handle, &result);
	}

	if (is_valid_policy_hnd(&handle)) {
		dcerpc_lsa_Close(b, mem_ctx, &handle, &result);
	}

	return status;
}


/* List of commands exported by this module */

struct cmd_set lsarpc_commands[] = {

	{
		.name = "LSARPC",
	},

	{
		.name               = "lsaquery",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_query_info_policy,
		.wfn                =  NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Query info policy",
		.usage              = "",
	},
	{
		.name               = "lookupsids",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_lookup_sids,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Convert SIDs to names",
		.usage              = "",
	},
	{
		.name               = "lookupsids3",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_lookup_sids3,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Convert SIDs to names",
		.usage              = "",
	},
	{
		.name               = "lookupsids_level",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_lookup_sids_level,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Convert SIDs to names",
		.usage              = "",
	},
	{
		.name               = "lookupnames",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_lookup_names,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Convert names to SIDs",
		.usage              = "",
	},
	{
		.name               = "lookupnames4",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_lookup_names4,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Convert names to SIDs",
		.usage              = "",
	},
	{
		.name               = "lookupnames_level",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_lookup_names_level,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Convert names to SIDs",
		.usage              = "",
	},
	{
		.name               = "enumtrust",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_enum_trust_dom,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Enumerate trusted domains",
		.usage              = "Usage: [preferred max number] [enum context (0)]",
	},
	{
		.name               = "enumprivs",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_enum_privilege,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Enumerate privileges",
		.usage              = "",
	},
	{
		.name               = "getdispname",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_get_dispname,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Get the privilege name",
		.usage              = "",
	},
	{
		.name               = "lsaenumsid",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_enum_sids,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Enumerate the LSA SIDS",
		.usage              = "",
	},
	{
		.name               = "lsacreateaccount",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_create_account,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Create a new lsa account",
		.usage              = "",
	},
	{
		.name               = "lsaenumprivsaccount",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_enum_privsaccounts,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Enumerate the privileges of an SID",
		.usage              = "",
	},
	{
		.name               = "lsaenumacctrights",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_enum_acct_rights,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Enumerate the rights of an SID",
		.usage              = "",
	},
	{
		.name               = "lsaaddpriv",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_add_priv,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Assign a privilege to a SID",
		.usage              = "",
	},
	{
		.name               = "lsadelpriv",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_del_priv,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Revoke a privilege from a SID",
		.usage              = "",
	},
	{
		.name               = "lsaaddacctrights",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_add_acct_rights,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Add rights to an account",
		.usage              = "",
	},
	{
		.name               = "lsaremoveacctrights",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_remove_acct_rights,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Remove rights from an account",
		.usage              = "",
	},
	{
		.name               = "lsalookupprivvalue",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_lookup_priv_value,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Get a privilege value given its name",
		.usage              = "",
	},
	{
		.name               = "lsaquerysecobj",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_query_secobj,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Query LSA security object",
		.usage              = "",
	},
	{
		.name               = "lsaquerytrustdominfo",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_query_trustdominfo,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Query LSA trusted domains info (given a SID)",
		.usage              = "",
	},
	{
		.name               = "lsaquerytrustdominfobyname",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_query_trustdominfobyname,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Query LSA trusted domains info (given a name), only works for Windows > 2k",
		.usage              = "",
	},
	{
		.name               = "lsaquerytrustdominfobysid",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_query_trustdominfobysid,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Query LSA trusted domains info (given a SID)",
		.usage              = "",
	},
	{
		.name               = "lsasettrustdominfo",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_set_trustdominfo,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Set LSA trusted domain info",
		.usage              = "",
	},
	{
		.name               = "getusername",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_get_username,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Get username",
		.usage              = "",
	},
	{
		.name               = "createsecret",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_create_secret,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Create Secret",
		.usage              = "",
	},
	{
		.name               = "deletesecret",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_delete_secret,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Delete Secret",
		.usage              = "",
	},
	{
		.name               = "querysecret",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_query_secret,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Query Secret",
		.usage              = "",
	},
	{
		.name               = "setsecret",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_set_secret,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Set Secret",
		.usage              = "",
	},
	{
		.name               = "retrieveprivatedata",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_retrieve_private_data,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Retrieve Private Data",
		.usage              = "",
	},
	{
		.name               = "storeprivatedata",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_store_private_data,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Store Private Data",
		.usage              = "",
	},
	{
		.name               = "createtrustdom",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_create_trusted_domain,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Create Trusted Domain",
		.usage              = "",
	},
	{
		.name               = "createtrustdomex2",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_create_trusted_domain_ex2,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Create Trusted Domain (Ex2 Variant)",
		.usage              = "",
	},
	{
		.name               = "createtrustdomex3",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_create_trusted_domain_ex3,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Create Trusted Domain (Ex3 Variant)",
		.usage              = "",
	},
	{
		.name               = "deletetrustdom",
		.returntype         = RPC_RTYPE_NTSTATUS,
		.ntfn               = cmd_lsa_delete_trusted_domain,
		.wfn                = NULL,
		.table              = &ndr_table_lsarpc,
		.rpc_pipe           = NULL,
		.description        = "Delete Trusted Domain",
		.usage              = "",
	},
	{
		.name = NULL,
	},
};
