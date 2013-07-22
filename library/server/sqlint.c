/* 
 * Copyright (C) Shivaram Upadhyayula <shivaram.u@quadstor.com>
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * Version 2 as published by the Free Software Foundation
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, 
 * Boston, MA  02110-1301, USA.
 */

#include <apicommon.h>
#include <tlsrvapi.h>
#include <vdevice.h>
#include <pgsql.h>
#include <ietadm.h>

int
sql_add_group(PGconn *conn, struct group_info *group_info)
{
	char sqlcmd[512];
	int error = -1;

	if (!group_info->group_id) {
		snprintf(sqlcmd, sizeof(sqlcmd), "INSERT INTO STORAGEGROUP (NAME, WORM) VALUES('%s', '%d')", group_info->name, group_info->worm);
		group_info->group_id = pgsql_exec_query3(conn, sqlcmd, 1, &error, "STORAGEGROUP", "GROUPID");
	}
	else {
		snprintf(sqlcmd, sizeof(sqlcmd), "INSERT INTO STORAGEGROUP (GROUPID, NAME, WORM) VALUES('%u', '%s', '%d')", group_info->group_id, group_info->name, group_info->worm);
		pgsql_exec_query3(conn, sqlcmd, 0, &error, NULL, NULL);
	}

	if (!group_info->group_id || error != 0)
		return -1;
	else
		return 0;
}

int
sql_delete_group(uint32_t group_id)
{
	char sqlcmd[128];
	int error = -1;

	snprintf(sqlcmd, sizeof(sqlcmd), "DELETE FROM STORAGEGROUP WHERE GROUPID='%u'", group_id);
	pgsql_exec_query2(sqlcmd, 0, &error, NULL, NULL);
	if (error < 0)
	{
		DEBUG_ERR_SERVER("Error occurred in executing sqlcmd %s\n", sqlcmd);
		return -1;
	}
	return 0;

}

int
sql_rename_pool(uint32_t group_id, char *name)
{
	char sqlcmd[128];
	int error = -1;

	snprintf(sqlcmd, sizeof(sqlcmd), "UPDATE STORAGEGROUP SET NAME='%s' WHERE GROUPID='%u'", name, group_id);
	pgsql_exec_query2(sqlcmd, 0, &error, NULL, NULL);
	if (error < 0)
	{
		DEBUG_ERR_SERVER("Error occurred in executing sqlcmd %s\n", sqlcmd);
		return -1;
	}
	return 0;
}

int sql_delete_all_tl_drives(int tl_id)
{
	char sqlcmd[100];
	int error;

	snprintf(sqlcmd, sizeof(sqlcmd), "DELETE FROM VDRIVES WHERE TLID='%d'", tl_id);
	pgsql_exec_query2(sqlcmd, 0, &error, NULL, NULL);
	if (error < 0)
	{
		DEBUG_ERR("failed for %s\n", sqlcmd);
		return -1;
	}
	return 0;

}

int
sql_update_blkdev_group_id(uint32_t bid, uint32_t group_id)
{
	char sqlcmd[256];
	int error;

	sprintf(sqlcmd, "UPDATE PHYSSTOR SET GROUPID='%u' WHERE BID='%u'", group_id, bid);
	pgsql_exec_query2(sqlcmd, 0, &error, NULL, NULL);
	return error;
}

PGconn *
sql_add_blkdev(struct physdisk *disk, uint32_t bid, uint32_t group_id)
{
	char *sqlcmd = NULL;
	int cmdlen;
	struct physdevice *device = (struct physdevice *)disk;
	int error = -1;
	PGconn *conn;
	unsigned char *t10id = NULL;
	unsigned char *naaid = NULL;
	unsigned char *euiid = NULL;
	unsigned char *unknownid = NULL;
	size_t len;
	unsigned char *t10esc = (unsigned char *)"NULL";
	unsigned char *naaesc = (unsigned char *)"NULL";
	unsigned char *euiesc = (unsigned char *)"NULL";
	unsigned char *unesc = (unsigned char *)"NULL";

	conn = pgsql_begin();
	if (!conn)
	{
		return NULL;
	}

	cmdlen = 512;
	if (device->idflags & ID_FLAGS_T10)
	{
		t10id = PQescapeByteaConn(conn, (const unsigned char *)(&device->t10_id), sizeof(device->t10_id), &len);
		if (!t10id)
		{
			DEBUG_ERR_SERVER("Unable to escape t10 id\n");
			goto err;
		}
		t10esc = t10id;
		cmdlen += len;
	}

	if (device->idflags & ID_FLAGS_NAA)
	{
		naaid = PQescapeByteaConn(conn, (const unsigned char *)(device->naa_id.naa_id), sizeof(device->naa_id.naa_id), &len);
		if (!naaid)
		{
			DEBUG_ERR_SERVER("Unable to escape naa id\n");
			goto err;
		}
		naaesc = naaid;
		cmdlen += len;
	}

	if (device->idflags & ID_FLAGS_EUI)
	{
		euiid = PQescapeByteaConn(conn, (const unsigned char *)(device->eui_id.eui_id), sizeof(device->eui_id.eui_id), &len);
		if (!euiid)
		{
			DEBUG_ERR_SERVER("Unable to escape eui id\n");
			goto err;
		}
		euiesc = euiid;
		cmdlen += len;
	}

	if (device->idflags & ID_FLAGS_UNKNOWN)
	{
		unknownid = PQescapeByteaConn(conn, (const unsigned char *)(device->unknown_id.unknown_id), sizeof(device->unknown_id.unknown_id), &len);
		if (!unknownid)
		{
			DEBUG_ERR_SERVER("Unable to escape unknown id\n");
			goto err;
		}
		unesc = unknownid;
		cmdlen += len;
	}

	sqlcmd = alloc_buffer(cmdlen);
	if (!sqlcmd)
	{
		DEBUG_ERR_SERVER("Memory allocation for %d bytes\n", cmdlen);
		goto err;
	}

	if (!bid) {
		snprintf(sqlcmd, cmdlen, "INSERT INTO PHYSSTOR (VENDOR, PRODUCT, IDFLAGS, T10ID, NAAID, EUI64ID, UNKNOWNID, ISRAID, RAIDDEV, PID, GROUPID) VALUES ('%.8s', '%.16s', '%u', '%s', '%s', '%s', '%s', '%d', '%s', '%d', '%u')", device->vendor, device->product, device->idflags, t10esc, naaesc, euiesc, unesc, disk->raiddisk, disk->raiddisk ? device->devname : "", disk->partid, group_id);
		bid = pgsql_exec_query3(conn, sqlcmd, 1, &error, "PHYSSTOR", "BID");
	}
	else {
		snprintf(sqlcmd, cmdlen, "INSERT INTO PHYSSTOR (BID, VENDOR, PRODUCT, IDFLAGS, T10ID, NAAID, EUI64ID, UNKNOWNID, ISRAID, RAIDDEV, PID, GROUPID) VALUES ('%u', '%.8s', '%.16s', '%u', '%s', '%s', '%s', '%s', '%d', '%s', '%d', '%u')", bid, device->vendor, device->product, device->idflags, t10esc, naaesc, euiesc, unesc, disk->raiddisk, disk->raiddisk ? device->devname : "", disk->partid, group_id);
		pgsql_exec_query3(conn, sqlcmd, 0, &error, NULL, NULL);
	}

	free(sqlcmd);

	if (error != 0)
		DEBUG_ERR_SERVER("sqlcmd execution failed with error %d bid %u\n", error, bid);
err:
	if (t10id)
		PQfreemem(t10id);
	if (naaid)
		PQfreemem(naaid);
	if (euiid)
		PQfreemem(euiid);
	if (unknownid)
		PQfreemem(unknownid);

	if (error != 0) {
		pgsql_rollback(conn);
		return NULL;
	} else
		return conn;
}

int
sql_delete_blkdev(struct tl_blkdevinfo *binfo)
{
	char sqlcmd[100];
	int error;

	snprintf(sqlcmd, sizeof(sqlcmd), "DELETE FROM PHYSSTOR WHERE BID='%u'", binfo->bid);
	pgsql_exec_query2(sqlcmd, 0, &error, NULL, NULL);
	if (error != 0)
	{
		DEBUG_ERR("failed for %s\n", sqlcmd);
		return -1;
	}
	return 0;
}

int
sql_delete_vcartridge(PGconn *conn, int tl_id, int tape_id)
{
	char sqlcmd[256];
	int error;

	snprintf(sqlcmd, sizeof(sqlcmd), "DELETE FROM VCARTRIDGE WHERE TAPEID='%d' AND TLID='%d'", tape_id, tl_id);
	pgsql_exec_query3(conn, sqlcmd, 0, &error, NULL, NULL);
	return error;
}

int
sql_add_vtl_drive(int tl_id, struct tdriveconf *tdriveconf)
{
	struct vdevice *vdevice = (struct vdevice *)(tdriveconf);
	char sqlcmd[512];
	int error;

	snprintf(sqlcmd, sizeof(sqlcmd), "INSERT INTO VDRIVES (TLID, TARGETID, DRIVETYPE, NAME, SERIALNUMBER) VALUES ('%d', '%u', '%d', '%s', '%s')", tl_id, vdevice->target_id, tdriveconf->type, vdevice->name, vdevice->serialnumber);

	pgsql_exec_query2(sqlcmd, 0, &error, NULL, NULL);
	return error;
}

int
sql_add_drive(struct tdriveconf *driveconf)
{
	struct vdevice *vdevice = (struct vdevice *)driveconf;
	char sqlcmd[512];
	int error;

	snprintf(sqlcmd, sizeof(sqlcmd), "INSERT INTO VTLS (VTLID, DEVTYPE, VTLNAME, VTLTYPE, SLOTS, IMPEXP, DRIVES, SERIALNUMBER) VALUES ('%d', '%d', '%s', '%d', '0', '0', '0', '%s')", vdevice->tl_id, T_SEQUENTIAL, vdevice->name, driveconf->type, vdevice->serialnumber);

	pgsql_exec_query2(sqlcmd, 0, &error, NULL, NULL);
	if (error != 0)
	{
		return -1;
	}

	error = sql_add_vtl_drive(vdevice->tl_id, driveconf);
	if (error != 0)
	{
		DEBUG_ERR("Inserting into table VDRIVES failed\n");
		goto err;
	}
	return 0;
err:
	snprintf(sqlcmd, sizeof(sqlcmd), "DELETE FROM VTLS WHERE VTLID='%d'", vdevice->tl_id);
	pgsql_exec_query2(sqlcmd, 0, &error, NULL, NULL);
	return -1;
}

int
sql_delete_vtl(int tl_id)
{
	char sqlcmd[128];
	int error;

	snprintf(sqlcmd, sizeof(sqlcmd), "DELETE FROM VTLS WHERE VTLID='%d'", tl_id);
	pgsql_exec_query2(sqlcmd, 0, &error, NULL, NULL);
	return error;
}

int
sql_update_iscsiconf(int tl_id, uint32_t target_id, struct iscsiconf *iscsiconf)
{
	char sqlcmd[512];
	int error;

	snprintf(sqlcmd, sizeof(sqlcmd), "UPDATE ISCSICONF SET INCOMINGUSER='%s', INCOMINGPASSWD='%s', OUTGOINGUSER='%s', OUTGOINGPASSWD='%s', IQN='%s' WHERE TLID='%d' AND TARGETID='%u'", iscsiconf->IncomingUser, iscsiconf->IncomingPasswd, iscsiconf->OutgoingUser, iscsiconf->OutgoingPasswd, iscsiconf->iqn, tl_id, target_id);
	DEBUG_INFO("cmd %s\n", sqlcmd);

	pgsql_exec_query2(sqlcmd, 0, &error, NULL, NULL);
	return error;
}

int
sql_add_iscsiconf(int tl_id, uint32_t target_id, struct iscsiconf *iscsiconf)
{
	char sqlcmd[512];
	int error;

	snprintf(sqlcmd, sizeof(sqlcmd), "INSERT INTO ISCSICONF (TLID, TARGETID, INCOMINGUSER, INCOMINGPASSWD, OUTGOINGUSER, OUTGOINGPASSWD, IQN) VALUES ('%d', '%u', '%s', '%s', '%s', '%s', '%s')", tl_id, target_id, iscsiconf->IncomingUser, iscsiconf->IncomingPasswd, iscsiconf->OutgoingUser, iscsiconf->OutgoingPasswd, iscsiconf->iqn);

	pgsql_exec_query2(sqlcmd, 0, &error, NULL, NULL);
	return error;
}

int
sql_add_vtl(struct vtlconf *vtlconf)
{
	struct vdevice *vdevice = (struct vdevice *)vtlconf;
	char sqlcmd[512];
	int error;

	snprintf(sqlcmd, sizeof(sqlcmd), "INSERT INTO VTLS (VTLID, DEVTYPE, VTLNAME, VTLTYPE, SLOTS, IMPEXP, DRIVES, SERIALNUMBER) VALUES ('%d', '%d', '%s', '%d', '%d', '%d', '%d', '%s')", vdevice->tl_id, T_CHANGER, vdevice->name, vtlconf->type, vtlconf->slots, vtlconf->ieports, vtlconf->drives, vdevice->serialnumber);
	pgsql_exec_query2(sqlcmd, 0, &error, NULL, NULL);
	return error;
}

int
sql_query_iscsiconf(int tl_id, uint32_t target_id, struct iscsiconf *iscsiconf)
{
	char sqlcmd[512];
	PGconn *conn;
	PGresult *res;
	int nrows;

	snprintf(sqlcmd, sizeof(sqlcmd), "SELECT INCOMINGUSER, INCOMINGPASSWD, OUTGOINGUSER, OUTGOINGPASSWD, IQN FROM ISCSICONF WHERE TLID='%d' AND TARGETID='%u'", tl_id, target_id);
	res = pgsql_exec_query(sqlcmd, &conn);
	if (res == NULL) {
		return -1;
	}
	nrows = PQntuples(res);
	if (nrows != 1)
	{
		DEBUG_ERR("Got more than one row\n");
		PQclear(res);
		PQfinish(conn);
		if (!nrows)
		{
			return -2;
		}
		return -1;
	}
	memcpy(iscsiconf->IncomingUser, PQgetvalue(res, 0, 0), PQgetlength(res, 0, 1));
	memcpy(iscsiconf->IncomingPasswd, PQgetvalue(res, 0, 1), PQgetlength(res, 0, 1));
	memcpy(iscsiconf->OutgoingUser, PQgetvalue(res, 0, 2), PQgetlength(res, 0, 2));
	memcpy(iscsiconf->OutgoingPasswd, PQgetvalue(res, 0, 3), PQgetlength(res, 0, 3));
	memcpy(iscsiconf->iqn, PQgetvalue(res, 0, 4), PQgetlength(res, 0, 4));

	iscsiconf->tl_id = tl_id;
	iscsiconf->target_id = target_id;
	PQclear(res);
	PQfinish(conn);
	return 0;
}

int
sql_query_driveprop(struct tdriveconf *driveconf)
{
	struct vdevice *vdevice = (struct vdevice *)driveconf;
	char sqlcmd[128];
	PGconn *conn;
	PGresult *res;
	int nrows;

	snprintf(sqlcmd, sizeof(sqlcmd), "SELECT DRIVETYPE FROM VDRIVES WHERE TLID='%d'", vdevice->tl_id);
	res = pgsql_exec_query(sqlcmd, &conn);
	if (res == NULL)
	{
		return -1;
	}

	nrows = PQntuples(res);
	if (nrows != 1)
	{
		DEBUG_ERR("Got more than one row\n");
		PQclear(res);
		PQfinish(conn);
		return -1;
	}	

	driveconf->type = atoi(PQgetvalue(res, 0, 0));

	PQclear(res);
	PQfinish(conn);
	return 0;
}

int
sql_query_drives(struct vtlconf *vtlconf)
{
	struct vdevice *vdevice = (struct vdevice *)vtlconf;
	char sqlcmd[128];
	PGconn *conn;
	PGresult *res;
	int nrows;
	int i;

	snprintf(sqlcmd, sizeof(sqlcmd), "SELECT TARGETID,NAME,DRIVETYPE,SERIALNUMBER FROM VDRIVES WHERE TLID='%d' ORDER BY TLID,TARGETID ASC", vdevice->tl_id);

	res = pgsql_exec_query(sqlcmd, &conn);
	if (res == NULL)
	{
		return -1;
	}

	nrows = PQntuples(res);
	for (i = 0; i < nrows; i++)
	{
		struct tdriveconf *driveconf;
		int retval;
		int target_id;

		target_id = strtoul(PQgetvalue(res, i, 0), NULL, 10);
		driveconf = tdriveconf_new(vdevice->tl_id, target_id, PQgetvalue(res, i, 1), PQgetvalue(res, i, 3));
		driveconf->type = atoi(PQgetvalue(res, i, 2));

		retval= sql_query_iscsiconf(vdevice->tl_id, target_id, &driveconf->vdevice.iscsiconf);
		if (retval != 0)
		{
			DEBUG_ERR("Query iscsiconf failed\n");
			free(driveconf);
			goto err;
		}
		vdevice_construct_iqn(&driveconf->vdevice, vdevice);
		TAILQ_INSERT_TAIL(&vtlconf->drive_list, driveconf, q_entry); 
	}

	PQclear(res);
	PQfinish(conn);
	return 0;

err:
	PQclear(res);
	PQfinish(conn);
	return -1;
}

int
sql_query_vdevice(struct vdevice *device_list[])
{
	char sqlcmd[128];
	int retval;
	PGconn *conn;
	PGresult *res;
	int nrows;
	int i;

	snprintf(sqlcmd, sizeof(sqlcmd), "SELECT VTLID,DEVTYPE,VTLNAME,VTLTYPE,SLOTS,IMPEXP,DRIVES,SERIALNUMBER FROM VTLS");

	res = pgsql_exec_query(sqlcmd, &conn);
	if (res == NULL)
	{
		return -1;
	}

	nrows = PQntuples(res);
	for (i = 0; i < nrows; i++)
	{
		int tl_id;
		int devtype;

		tl_id = atoi(PQgetvalue(res, i, 0));
		devtype = atoi(PQgetvalue(res, i, 1));
		if (devtype == T_CHANGER) {
			struct vtlconf *vtlconf;

			vtlconf = vtlconf_new(tl_id, PQgetvalue(res, i, 2), PQgetvalue(res, i, 7));
			if (!vtlconf) {
				DEBUG_ERR("Unable to allocc for a new vtlconf struct\n");
				goto err;
			}

			vtlconf->type = atoi(PQgetvalue(res, i, 3));
			vtlconf->slots = atoi(PQgetvalue(res, i, 4));
			vtlconf->ieports = atoi(PQgetvalue(res, i, 5));
			vtlconf->drives = atoi(PQgetvalue(res, i, 6));

			retval = sql_query_drives(vtlconf);
			if (retval != 0) {
				DEBUG_ERR("Unable to load drives for the vtl\n");
				free(vtlconf);
				goto err;
			}

			retval= sql_query_iscsiconf(tl_id, 0, &vtlconf->vdevice.iscsiconf);
			if (retval != 0) {
				DEBUG_ERR("Query iscsiconf failed\n");
				free(vtlconf);
				goto err;
			}
			vdevice_construct_iqn(&vtlconf->vdevice, NULL);
			device_list[tl_id] = (struct vdevice *)vtlconf;
		}
		else
		{
			struct tdriveconf *driveconf;

			driveconf = tdriveconf_new(tl_id, 0, PQgetvalue(res, i, 2), PQgetvalue(res, i, 7));
			if (!driveconf) {
				DEBUG_ERR("Unable to allocae for a new driveconf struct\n");
				goto err;
			}
			driveconf->type = atoi(PQgetvalue(res, i, 3));
			retval = sql_query_driveprop(driveconf);
			if (retval != 0) {
				DEBUG_ERR("Query drive property failed\n");
				free(driveconf);
				goto err;
			}

			retval= sql_query_iscsiconf(tl_id, 0, &driveconf->vdevice.iscsiconf);
			if (retval != 0) {
				DEBUG_ERR("Query iscsiconf failed\n");
				free(driveconf);
				goto err;
			}
			vdevice_construct_iqn(&driveconf->vdevice, NULL);
			device_list[tl_id] = (struct vdevice *)driveconf;
		}
	}

	PQclear(res);
	PQfinish(conn);
	return 0;
err:

	PQclear(res);
	PQfinish(conn);
	for (i = 0; i < TL_MAX_DEVICES; i++)
	{
		struct vdevice *vdevice = device_list[i];
		if (!vdevice)
		{
			continue;
		}
		free_vdevice(vdevice);
		device_list[i] = NULL;
	}
	return -1;
}

int
sql_set_volume_exported(struct vcartridge *vinfo)
{
	char sqlcmd[128];
	int error;

	snprintf(sqlcmd, sizeof(sqlcmd), "UPDATE VCARTRIDGE SET VSTATUS='%d' WHERE TLID='%d' AND TAPEID='%u'", MEDIA_STATUS_EXPORTED, vinfo->tl_id, vinfo->tape_id);

	pgsql_exec_query2(sqlcmd, 0, &error, NULL, NULL);
	if (error != 0)
	{
		return -1;
	}

	return 0;
}

int
sql_add_vcartridge(PGconn *conn, struct vcartridge *vinfo)
{
	char sqlcmd[512];
	int error = -1;

	if (!vinfo->tape_id) {
		snprintf(sqlcmd, sizeof(sqlcmd), "INSERT INTO VCARTRIDGE (GROUPID, TLID, VTYPE, LABEL, VSIZE, VSTATUS, WORM) VALUES ('%u', '%d', '%d', '%s', '%llu', '%u', '%d')", vinfo->group_id, vinfo->tl_id, vinfo->type, vinfo->label, (unsigned long long)vinfo->size, vinfo->vstatus, vinfo->worm);
		vinfo->tape_id = pgsql_exec_query3(conn, sqlcmd, 1, &error, "VCARTRIDGE" , "TAPEID");
	}
	else {
		snprintf(sqlcmd, sizeof(sqlcmd), "INSERT INTO VCARTRIDGE (TAPEID, GROUPID, TLID, VTYPE, LABEL, VSIZE, VSTATUS, WORM) VALUES ('%u', '%u', '%d', '%d', '%s', '%llu', '%u', '%d')", vinfo->tape_id, vinfo->group_id, vinfo->tl_id, vinfo->type, vinfo->label, (unsigned long long)vinfo->size, vinfo->vstatus, vinfo->worm);
		pgsql_exec_query3(conn, sqlcmd, 0, &error, NULL, NULL);
	}

	if (!vinfo->tape_id || error != 0)
		return -1;
	else
		return 0;
}

int
sql_query_volumes(struct tl_blkdevinfo *binfo)
{
	char sqlcmd[512];
	PGconn *conn;
	PGresult *res;
	int nrows;
	int i;
	struct vcartridge *vinfo;

	snprintf(sqlcmd, sizeof(sqlcmd), "SELECT TAPEID,TLID,VTYPE,LABEL,VSIZE,VSTATUS,WORM,EADDRESS FROM VCARTRIDGE WHERE GROUPID='%u' ORDER BY TAPEID", binfo->group->group_id);

	res = pgsql_exec_query(sqlcmd, &conn);
	if (res == NULL)
	{
		DEBUG_ERR("Error occurrd in executing sqlcmd %s\n", sqlcmd); 
		return -1;
	}

	nrows = PQntuples(res);
	for (i = 0; i < nrows; i++)
	{
		vinfo = alloc_buffer(sizeof(struct vcartridge));
		if (!vinfo) {
			DEBUG_ERR("Unable to allocate for a new volumeinfo struct\n");
			goto err;
		}
		vinfo->group_id = binfo->group->group_id;
		strcpy(vinfo->group_name, binfo->group->name);
		vinfo->tape_id = atoi(PQgetvalue(res, i, 0));
		vinfo->tl_id = atoi(PQgetvalue(res, i, 1));
		vinfo->type = atoi(PQgetvalue(res, i, 2));
		memcpy(vinfo->label, PQgetvalue(res, i, 3), PQgetlength(res, i, 3));
		vinfo->size = strtoull(PQgetvalue(res, i, 4), NULL, 10);
		vinfo->worm = strtoul(PQgetvalue(res, i, 6), NULL, 10);
		vinfo->elem_address = atoi(PQgetvalue(res, i, 7));
		TAILQ_INSERT_TAIL(&binfo->vol_list, vinfo, q_entry);
	}

	PQclear(res);
	PQfinish(conn);
	return 0;
err:
	PQclear(res);
	PQfinish(conn);
	while ((vinfo = TAILQ_FIRST(&binfo->vol_list)))
	{
		TAILQ_REMOVE(&binfo->vol_list, vinfo, q_entry);
		free(vinfo);
	}
	return -1;
}

int
sql_query_groups(struct group_info *group_list[])
{
	char sqlcmd[128];
	PGconn *conn;
	PGresult *res;
	int nrows;
	int i, error = 0;
	struct group_info *group_info;

	snprintf(sqlcmd, sizeof(sqlcmd), "SELECT GROUPID,NAME,WORM FROM STORAGEGROUP ORDER BY GROUPID");

	res = pgsql_exec_query(sqlcmd, &conn);
	if (res == NULL) {
		DEBUG_ERR_SERVER("Error occurred in executing sqlcmd %s\n", sqlcmd); 
		return -1;
	}

	nrows = PQntuples(res);
	for (i = 0; i < nrows; i++) {
		group_info = malloc(sizeof(struct group_info));
		if (!group_info) {
			PQclear(res);
			PQfinish(conn);
			return -1;
		}

		memset(group_info, 0, sizeof(struct group_info));
		group_info->group_id = atoi(PQgetvalue(res, i, 0));
		memcpy(group_info->name, PQgetvalue(res, i, 1), PQgetlength(res, i, 1));
		group_info->worm = atoi(PQgetvalue(res, i, 2));
		TAILQ_INIT(&group_info->bdev_list);
		group_list[group_info->group_id] = group_info;
	}

	PQclear(res);
	PQfinish(conn);
	return error;
}

int
sql_query_blkdevs(struct tl_blkdevinfo *bdev_list[])
{
	char sqlcmd[256];
	PGconn *conn;
	PGresult *res;
	int nrows;
	int i;
	struct tl_blkdevinfo *binfo;

	snprintf(sqlcmd, sizeof(sqlcmd), "SELECT BID,VENDOR,PRODUCT,IDFLAGS,T10ID::bytea,NAAID::bytea,EUI64ID::bytea,UNKNOWNID::bytea,PID,ISRAID,RAIDDEV,GROUPID FROM PHYSSTOR");

	res = pgsql_exec_query(sqlcmd, &conn);
	if (res == NULL)
	{
		DEBUG_ERR("Error occurrd in executing sqlcmd %s\n", sqlcmd); 
		return -1;
	}

	nrows = PQntuples(res);
	for (i = 0; i < nrows; i++)
	{
		struct physdisk *disk;
		struct physdevice *device;

		binfo = malloc(sizeof(struct tl_blkdevinfo));
		if (!binfo)
		{
			DEBUG_ERR("Unable to alloc a new blkdev struct\n");
			goto err;
		}

		memset(binfo, 0, sizeof(struct tl_blkdevinfo));
		disk = &binfo->disk;
		device = (struct physdevice *)(disk);
		strcpy(device->devname, "none");
		binfo->bid = strtoull(PQgetvalue(res, i, 0), NULL, 10);
		disk->partid = strtoull(PQgetvalue(res, i, 8), NULL, 10);
		disk->raiddisk = strtoull(PQgetvalue(res, i, 9), NULL, 10);
		if (disk->raiddisk)
		{
			strcpy(device->devname, PQgetvalue(res, i, 10));
		}
		binfo->group_id = strtoull(PQgetvalue(res, i, 0), NULL, 11);
		binfo->db_group_id = binfo->group_id;
		if (PQgetlength(res, i, 1) != 8)
		{
			DEBUG_ERR("Got invalid length for vendor %d\n", PQgetlength(res, i, 1));
			goto err;
		}
		memcpy(device->vendor, PQgetvalue(res, i, 1), 8);
		if (PQgetlength(res, i, 2) != 16)
		{
			DEBUG_ERR("Got invalid length for product %d\n", PQgetlength(res, i, 2));
			goto err;
		}
		memcpy(device->product, PQgetvalue(res, i, 2), 16);

		device->idflags = strtoul(PQgetvalue(res, i, 3), NULL, 10);

		if (device->idflags & ID_FLAGS_T10)
		{
			uint8_t *ptr;
			size_t len;

			ptr = PQunescapeBytea((const unsigned char *)PQgetvalue(res, i, 4), &len);
			if (!ptr)
			{
				DEBUG_ERR("Unescaping binary string failed\n");
				goto err;
			}

			if (len != sizeof(struct device_t10_id))
			{
				DEBUG_ERR("Got invalid length for t10id %d\n", (int)len);
				PQfreemem(ptr);
				goto err;
			}
			memcpy(&device->t10_id, ptr, sizeof(struct device_t10_id));
			PQfreemem(ptr);
		}

		if (device->idflags & ID_FLAGS_NAA)
		{
			uint8_t *ptr;
			size_t len;

			ptr = PQunescapeBytea((const unsigned char *)PQgetvalue(res, i, 5), &len);
			if (!ptr)
			{
				DEBUG_ERR("Unescaping binary string failed\n");
				goto err;
			}

			if (len != sizeof(device->naa_id.naa_id))
			{
				DEBUG_ERR("Got invalid length for naaid %d\n", (int)len);
				PQfreemem(ptr);
				goto err;
			}
			memcpy(device->naa_id.naa_id, ptr, sizeof(device->naa_id.naa_id));
			PQfreemem(ptr);
		}

		if (device->idflags & ID_FLAGS_EUI)
		{
			uint8_t *ptr;
			size_t len;

			ptr = PQunescapeBytea((const unsigned char *)PQgetvalue(res, i, 6), &len);
			if (!ptr)
			{
				DEBUG_ERR("Unescaping binary string failed\n");
				goto err;
			}

			if (len != sizeof(device->eui_id.eui_id))
			{
				DEBUG_ERR("Got invalid length for euiid %d\n", (int)len);
				PQfreemem(ptr);
				goto err;
			}
			memcpy(device->eui_id.eui_id, ptr, sizeof(device->eui_id.eui_id));
			PQfreemem(ptr);
		}

		if (device->idflags & ID_FLAGS_UNKNOWN)
		{
			uint8_t *ptr;
			size_t len;

			ptr = PQunescapeBytea((const unsigned char *)PQgetvalue(res, i, 7), &len);
			if (!ptr)
			{
				DEBUG_ERR("Unescaping binary string failed\n");
				goto err;
			}

			if (len != sizeof(device->unknown_id.unknown_id))
			{
				DEBUG_ERR("Got invalid length for unknownid %d\n", (int)len);
				PQfreemem(ptr);
				goto err;
			}
			memcpy(device->unknown_id.unknown_id, ptr, sizeof(device->unknown_id.unknown_id));
			PQfreemem(ptr);
		}

		TAILQ_INIT(&binfo->vol_list);

		DEBUG_BUG_ON(bdev_list[binfo->bid]);
		bdev_list[binfo->bid] = binfo;
	}

	PQclear(res);
	PQfinish(conn);
	return 0;
err:
	if (binfo)
	{
		free(binfo);
	}
	PQclear(res);
	PQfinish(conn);
	return -1;
}

#define MAX_ID_FOR_RANGE 999

int
sql_virtvol_label_unique(char *label)
{
	char sqlcmd[128];
	PGconn *conn;
	PGresult *res;
	int nrows;

	snprintf(sqlcmd, sizeof(sqlcmd), "SELECT LABEL FROM VCARTRIDGE WHERE LOWER(LABEL) = LOWER('%s')", label);

	res = pgsql_exec_query(sqlcmd, &conn);
	if (res == NULL)
	{
		DEBUG_ERR("error occurred in executing sqlcmd %s\n", sqlcmd); 
		return -1;
	}

	nrows = PQntuples(res);
	PQclear(res);
	PQfinish(conn);
	if (nrows > 0)
	{
		return -1;
	}
	return 0;
}

int
sql_query_fc_rules(struct fc_rule_list *fc_rule_list)
{
	char sqlcmd[128];
	PGconn *conn;
	PGresult *res;
	int nrows;
	int i;
	struct fc_rule *fc_rule;

	snprintf(sqlcmd, sizeof(sqlcmd), "SELECT WWPN,WWPN1,TARGETID,RULE FROM FCCONFIG ORDER BY TARGETID");
	res = pgsql_exec_query(sqlcmd, &conn);
	if (res == NULL) {
		DEBUG_ERR_SERVER("Error occurred in executing sqlcmd %s\n", sqlcmd); 
		return -1;
	}

	nrows = PQntuples(res);
	for (i = 0; i < nrows; i++) {
		fc_rule = alloc_buffer(sizeof(*fc_rule));
		if (!fc_rule) {
			PQclear(res);
			PQfinish(conn);
			return -1;
		}

		strcpy(fc_rule->wwpn, PQgetvalue(res, i, 0));
		strcpy(fc_rule->wwpn1, PQgetvalue(res, i, 1));
		fc_rule->target_id = atoi(PQgetvalue(res, i, 2));
		fc_rule->rule = atoi(PQgetvalue(res, i, 3));
		TAILQ_INSERT_TAIL(fc_rule_list, fc_rule, q_entry);
	}

	PQclear(res);
	PQfinish(conn);
	return 0;

}

int
sql_add_fc_rule(struct fc_rule *fc_rule)
{
	char sqlcmd[512];
	int error = -1;

	snprintf(sqlcmd, sizeof(sqlcmd), "INSERT INTO FCCONFIG (WWPN, WWPN1, TARGETID, RULE) VALUES ('%s', '%s', '%d', '%d')", fc_rule->wwpn, fc_rule->wwpn1, fc_rule->target_id, fc_rule->rule);
	pgsql_exec_query2(sqlcmd, 0, &error, NULL, NULL);
	return error;
}

int
sql_delete_fc_rule(struct fc_rule *fc_rule)
{
	char sqlcmd[512];
	int error = -1;

	snprintf(sqlcmd, sizeof(sqlcmd), "DELETE FROM FCCONFIG WHERE WWPN='%s' AND WWPN1='%s' AND TARGETID='%d'", fc_rule->wwpn, fc_rule->wwpn1, fc_rule->target_id);
	pgsql_exec_query2(sqlcmd, 0, &error, NULL, NULL);
	return error;
}

int
sql_delete_vtl_fc_rules(int tl_id)
{
	char sqlcmd[128];
	int error = -1;

	snprintf(sqlcmd, sizeof(sqlcmd), "DELETE FROM FCCONFIG WHERE TARGETID='%d'", tl_id);
	pgsql_exec_query2(sqlcmd, 0, &error, NULL, NULL);
	return error;
}

int
sql_clear_slot_configuration(PGconn *conn, int tl_id)
{
	char sqlcmd[128];
	int error = 0;

	snprintf(sqlcmd, sizeof(sqlcmd), "UPDATE VCARTRIDGE set EADDRESS='0' WHERE TLID='%d'", tl_id);
	pgsql_exec_query3(conn, sqlcmd, 0, &error, NULL, NULL);
	return error;
}

int
sql_update_element_address(PGconn *conn, int tl_id, int tid, int eaddress)
{
	char sqlcmd[128];
	int error = 0;

	snprintf(sqlcmd, sizeof(sqlcmd), "UPDATE VCARTRIDGE set EADDRESS='%d' WHERE TLID='%d' AND TAPEID='%d'", eaddress, tl_id, tid);
	pgsql_exec_query3(conn, sqlcmd, 0, &error, NULL, NULL);
	return error;
}

