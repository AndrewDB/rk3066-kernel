/* Copyright (C) 2006, Red Hat, Inc. */

#include <linux/types.h>
#include <linux/etherdevice.h>
#include <linux/ieee80211.h>
#include <linux/if_arp.h>
#include <asm/atomic.h>

#include "lib80211.h"

#include "assoc.h"
#include "decl.h"
#include "host.h"
#include "scan.h"
#include "cmd.h"

/*
 * Defined in main.c
 */
extern atomic_t reassoc_flag;

#if (MV8686_SUPPORT_ADHOC == 1)
static int lbs_adhoc_post(struct lbs_private *priv, struct cmd_header *resp);
#endif

static const u8 bssid_any[ETH_ALEN]  __attribute__ ((aligned (2))) =
	{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static const u8 bssid_off[ETH_ALEN]  __attribute__ ((aligned (2))) =
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

/* The firmware needs certain bits masked out of the beacon-derviced capability
 * field when associating/joining to BSSs.
 */
#define CAPINFO_MASK	(~(0xda00))


/**
 *  @brief This function finds common rates between rates and card rates.
 *
 * It will fill common rates in rates as output if found.
 *
 * NOTE: Setting the MSB of the basic rates need to be taken
 *   care, either before or after calling this function
 *
 *  @param priv     A pointer to struct lbs_private structure
 *  @param rates       the buffer which keeps input and output
 *  @param rates_size  the size of rate1 buffer; new size of buffer on return
 *
 *  @return            0 on success, or -1 on error
 */
static int get_common_rates(struct lbs_private *priv,
	u8 *rates,
	u16 *rates_size)
{
	u8 *card_rates = lbs_bg_rates;
	size_t num_card_rates = sizeof(lbs_bg_rates);
	int ret = 0, i, j;
	u8 tmp[30];
	size_t tmp_size = 0;

	/* For each rate in card_rates that exists in rate1, copy to tmp */
	for (i = 0; card_rates[i] && (i < num_card_rates); i++) {
		for (j = 0; rates[j] && (j < *rates_size); j++) {
			if (rates[j] == card_rates[i])
				tmp[tmp_size++] = card_rates[i];
		}
	}

	lbs_deb_hex(LBS_DEB_JOIN, "AP rates    ", rates, *rates_size);
	lbs_deb_hex(LBS_DEB_JOIN, "card rates  ", card_rates, num_card_rates);
	lbs_deb_hex(LBS_DEB_JOIN, "common rates", tmp, tmp_size);
	lbs_deb_join("TX data rate 0x%02x\n", priv->cur_rate);

	if (!priv->enablehwauto) {
		for (i = 0; i < tmp_size; i++) {
			if (tmp[i] == priv->cur_rate)
				goto done;
		}
		lbs_pr_alert("Previously set fixed data rate %#x isn't "
		       "compatible with the network.\n", priv->cur_rate);
		ret = -1;
		goto done;
	}
	ret = 0;

done:
	memset(rates, 0, *rates_size);
	*rates_size = min_t(int, tmp_size, *rates_size);
	memcpy(rates, tmp, *rates_size);
	return ret;
}


/**
 *  @brief Sets the MSB on basic rates as the firmware requires
 *
 * Scan through an array and set the MSB for basic data rates.
 *
 *  @param rates     buffer of data rates
 *  @param len       size of buffer
 */
static void lbs_set_basic_rate_flags(u8 *rates, size_t len)
{
	int i;

	for (i = 0; i < len; i++) {
		if (rates[i] == 0x02 || rates[i] == 0x04 ||
		    rates[i] == 0x0b || rates[i] == 0x16)
			rates[i] |= 0x80;
	}
}


/**
 *  @brief Associate to a specific BSS discovered in a scan
 *
 *  @param priv      A pointer to struct lbs_private structure
 *  @param assoc_req The association request describing the BSS to associate with
 *
 *  @return          0-success, otherwise fail
 */
static int lbs_associate(struct lbs_private *priv,
	struct assoc_request *assoc_req)
{
	int ret;
	u8 preamble = RADIO_PREAMBLE_LONG;

	lbs_deb_enter(LBS_DEB_ASSOC);

//#if (NEW_MV8686_PS == 1)
#if 0
	assoc_retry:
#endif

	//printk("%s: sending authenticate command.\n", __func__);
	ret = lbs_prepare_and_send_command(priv, CMD_802_11_AUTHENTICATE,
				    0, CMD_OPTION_WAITFORRSP,
				    0, assoc_req->bss.bssid);
	if (ret)
		goto out;

	/* Use short preamble only when both the BSS and firmware support it */
	if ((priv->capability & WLAN_CAPABILITY_SHORT_PREAMBLE) &&
	    (assoc_req->bss.capability & WLAN_CAPABILITY_SHORT_PREAMBLE))
		preamble = RADIO_PREAMBLE_SHORT;

	ret = lbs_set_radio(priv, preamble, 1);
	if (ret)
		goto out;

	//printk("%s: sending associate command.\n", __func__);
	ret = lbs_prepare_and_send_command(priv, CMD_802_11_ASSOCIATE,
				    0, CMD_OPTION_WAITFORRSP, 0, assoc_req);
	
//#if (NEW_MV8686_PS == 1)
#if 0
	if (priv->associate_retry > 0)
	{
		printk("%s: Need to retry association.\n", __func__);
		goto assoc_retry;
	}
#endif

out:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}

#if (MV8686_SUPPORT_ADHOC == 1)

/**
 *  @brief Join an adhoc network found in a previous scan
 *
 *  @param priv         A pointer to struct lbs_private structure
 *  @param assoc_req    The association request describing the BSS to join
 *
 *  @return             0 on success, error on failure
 */
static int lbs_adhoc_join(struct lbs_private *priv,
	struct assoc_request *assoc_req)
{
	struct cmd_ds_802_11_ad_hoc_join cmd;
	struct bss_descriptor *bss = &assoc_req->bss;
	u8 preamble = RADIO_PREAMBLE_LONG;
	DECLARE_SSID_BUF(ssid);
	u16 ratesize = 0;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_ASSOC);

	lbs_deb_join("current SSID '%s', ssid length %u\n",
		mv8686_print_ssid(ssid, priv->curbssparams.ssid,
		priv->curbssparams.ssid_len),
		priv->curbssparams.ssid_len);
	lbs_deb_join("requested ssid '%s', ssid length %u\n",
		mv8686_print_ssid(ssid, bss->ssid, bss->ssid_len),
		bss->ssid_len);

	/* check if the requested SSID is already joined */
	if (priv->curbssparams.ssid_len &&
	    !lbs_ssid_cmp(priv->curbssparams.ssid,
			priv->curbssparams.ssid_len,
			bss->ssid, bss->ssid_len) &&
	    (priv->mode == IW_MODE_ADHOC) &&
	    (priv->connect_status == LBS_CONNECTED)) {
		union iwreq_data wrqu;

		lbs_deb_join("ADHOC_J_CMD: New ad-hoc SSID is the same as "
			"current, not attempting to re-join");

		/* Send the re-association event though, because the association
		 * request really was successful, even if just a null-op.
		 */
		memset(&wrqu, 0, sizeof(wrqu));
		memcpy(wrqu.ap_addr.sa_data, priv->curbssparams.bssid,
		       ETH_ALEN);
		wrqu.ap_addr.sa_family = ARPHRD_ETHER;
		wireless_send_event(priv->dev, SIOCGIWAP, &wrqu, NULL);
		goto out;
	}

	/* Use short preamble only when both the BSS and firmware support it */
	if ((priv->capability & WLAN_CAPABILITY_SHORT_PREAMBLE) &&
	    (bss->capability & WLAN_CAPABILITY_SHORT_PREAMBLE)) {
		lbs_deb_join("AdhocJoin: Short preamble\n");
		preamble = RADIO_PREAMBLE_SHORT;
	}

	ret = lbs_set_radio(priv, preamble, 1);
	if (ret)
		goto out;

	lbs_deb_join("AdhocJoin: channel = %d\n", assoc_req->channel);
	lbs_deb_join("AdhocJoin: band = %c\n", assoc_req->band);

	priv->adhoccreate = 0;
	priv->curbssparams.channel = bss->channel;

	/* Build the join command */
	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));

	cmd.bss.type = CMD_BSS_TYPE_IBSS;
	cmd.bss.beaconperiod = cpu_to_le16(bss->beaconperiod);

	memcpy(&cmd.bss.bssid, &bss->bssid, ETH_ALEN);
	memcpy(&cmd.bss.ssid, &bss->ssid, bss->ssid_len);

	memcpy(&cmd.bss.phyparamset, &bss->phyparamset,
	       sizeof(union ieeetypes_phyparamset));

	memcpy(&cmd.bss.ssparamset, &bss->ssparamset,
	       sizeof(union IEEEtypes_ssparamset));

	cmd.bss.capability = cpu_to_le16(bss->capability & CAPINFO_MASK);
	lbs_deb_join("ADHOC_J_CMD: tmpcap=%4X CAPINFO_MASK=%4X\n",
	       bss->capability, CAPINFO_MASK);

	/* information on BSSID descriptor passed to FW */
	lbs_deb_join("ADHOC_J_CMD: BSSID = %pM, SSID = '%s'\n",
			cmd.bss.bssid, cmd.bss.ssid);

	/* Only v8 and below support setting these */
	if (priv->fwrelease < 0x09000000) {
		/* failtimeout */
		cmd.failtimeout = cpu_to_le16(MRVDRV_ASSOCIATION_TIME_OUT);
		/* probedelay */
		cmd.probedelay = cpu_to_le16(CMD_SCAN_PROBE_DELAY_TIME);
	}

	/* Copy Data rates from the rates recorded in scan response */
	memset(cmd.bss.rates, 0, sizeof(cmd.bss.rates));
	ratesize = min_t(u16, sizeof(cmd.bss.rates), MAX_RATES);
	memcpy(cmd.bss.rates, bss->rates, ratesize);
	if (get_common_rates(priv, cmd.bss.rates, &ratesize)) {
		lbs_deb_join("ADHOC_JOIN: get_common_rates returned error.\n");
		ret = -1;
		goto out;
	}

	/* Copy the ad-hoc creation rates into Current BSS state structure */
	memset(&priv->curbssparams.rates, 0, sizeof(priv->curbssparams.rates));
	memcpy(&priv->curbssparams.rates, cmd.bss.rates, ratesize);

	/* Set MSB on basic rates as the firmware requires, but _after_
	 * copying to current bss rates.
	 */
	lbs_set_basic_rate_flags(cmd.bss.rates, ratesize);

	cmd.bss.ssparamset.ibssparamset.atimwindow = cpu_to_le16(bss->atimwindow);

	if (assoc_req->secinfo.wep_enabled) {
		u16 tmp = le16_to_cpu(cmd.bss.capability);
		tmp |= WLAN_CAPABILITY_PRIVACY;
		cmd.bss.capability = cpu_to_le16(tmp);
	}

	if (priv->psmode == LBS802_11POWERMODEMAX_PSP) {
		__le32 local_ps_mode = cpu_to_le32(LBS802_11POWERMODECAM);

		/* wake up first */
		ret = lbs_prepare_and_send_command(priv, CMD_802_11_PS_MODE,
						   CMD_ACT_SET, 0, 0,
						   &local_ps_mode);
		if (ret) {
			ret = -1;
			goto out;
		}
	}

	if (lbs_parse_dnld_countryinfo_11d(priv, bss)) {
		ret = -1;
		goto out;
	}

	ret = lbs_cmd_with_response(priv, CMD_802_11_AD_HOC_JOIN, &cmd);
	if (ret == 0)
		ret = lbs_adhoc_post(priv, (struct cmd_header *) &cmd);

out:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}

/**
 *  @brief Start an Adhoc Network
 *
 *  @param priv         A pointer to struct lbs_private structure
 *  @param assoc_req    The association request describing the BSS to start
 *
 *  @return             0 on success, error on failure
 */
static int lbs_adhoc_start(struct lbs_private *priv,
	struct assoc_request *assoc_req)
{
	struct cmd_ds_802_11_ad_hoc_start cmd;
	u8 preamble = RADIO_PREAMBLE_LONG;
	size_t ratesize = 0;
	u16 tmpcap = 0;
	int ret = 0;
	DECLARE_SSID_BUF(ssid);

	lbs_deb_enter(LBS_DEB_ASSOC);

	if (priv->capability & WLAN_CAPABILITY_SHORT_PREAMBLE) {
		lbs_deb_join("ADHOC_START: Will use short preamble\n");
		preamble = RADIO_PREAMBLE_SHORT;
	}

	ret = lbs_set_radio(priv, preamble, 1);
	if (ret)
		goto out;

	/* Build the start command */
	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));

	memcpy(cmd.ssid, assoc_req->ssid, assoc_req->ssid_len);

	lbs_deb_join("ADHOC_START: SSID '%s', ssid length %u\n",
		mv8686_print_ssid(ssid, assoc_req->ssid, assoc_req->ssid_len),
		assoc_req->ssid_len);

	cmd.bsstype = CMD_BSS_TYPE_IBSS;

	if (priv->beacon_period == 0)
		priv->beacon_period = MRVDRV_BEACON_INTERVAL;
	cmd.beaconperiod = cpu_to_le16(priv->beacon_period);

	WARN_ON(!assoc_req->channel);

	/* set Physical parameter set */
	cmd.phyparamset.dsparamset.elementid = WLAN_EID_DS_PARAMS;
	cmd.phyparamset.dsparamset.len = 1;
	cmd.phyparamset.dsparamset.currentchan = assoc_req->channel;

	/* set IBSS parameter set */
	cmd.ssparamset.ibssparamset.elementid = WLAN_EID_IBSS_PARAMS;
	cmd.ssparamset.ibssparamset.len = 2;
	cmd.ssparamset.ibssparamset.atimwindow = 0;

	/* set capability info */
	tmpcap = WLAN_CAPABILITY_IBSS;
	if (assoc_req->secinfo.wep_enabled) {
		lbs_deb_join("ADHOC_START: WEP enabled, setting privacy on\n");
		tmpcap |= WLAN_CAPABILITY_PRIVACY;
	} else
		lbs_deb_join("ADHOC_START: WEP disabled, setting privacy off\n");

	cmd.capability = cpu_to_le16(tmpcap);

	/* Only v8 and below support setting probe delay */
	if (priv->fwrelease < 0x09000000)
		cmd.probedelay = cpu_to_le16(CMD_SCAN_PROBE_DELAY_TIME);

	ratesize = min(sizeof(cmd.rates), sizeof(lbs_bg_rates));
	memcpy(cmd.rates, lbs_bg_rates, ratesize);

	/* Copy the ad-hoc creating rates into Current BSS state structure */
	memset(&priv->curbssparams.rates, 0, sizeof(priv->curbssparams.rates));
	memcpy(&priv->curbssparams.rates, &cmd.rates, ratesize);

	/* Set MSB on basic rates as the firmware requires, but _after_
	 * copying to current bss rates.
	 */
	lbs_set_basic_rate_flags(cmd.rates, ratesize);

	lbs_deb_join("ADHOC_START: rates=%02x %02x %02x %02x\n",
	       cmd.rates[0], cmd.rates[1], cmd.rates[2], cmd.rates[3]);

	if (lbs_create_dnld_countryinfo_11d(priv)) {
		lbs_deb_join("ADHOC_START: dnld_countryinfo_11d failed\n");
		ret = -1;
		goto out;
	}

	lbs_deb_join("ADHOC_START: Starting Ad-Hoc BSS on channel %d, band %d\n",
		     assoc_req->channel, assoc_req->band);

	priv->adhoccreate = 1;
	priv->mode = IW_MODE_ADHOC;

	ret = lbs_cmd_with_response(priv, CMD_802_11_AD_HOC_START, &cmd);
	if (ret == 0)
		ret = lbs_adhoc_post(priv, (struct cmd_header *) &cmd);

out:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}

/**
 *  @brief Stop and Ad-Hoc network and exit Ad-Hoc mode
 *
 *  @param priv         A pointer to struct lbs_private structure
 *  @return             0 on success, or an error
 */
int lbs_adhoc_stop(struct lbs_private *priv)
{
	struct cmd_ds_802_11_ad_hoc_stop cmd;
	int ret;

	lbs_deb_enter(LBS_DEB_JOIN);

	memset(&cmd, 0, sizeof (cmd));
	cmd.hdr.size = cpu_to_le16 (sizeof (cmd));

	ret = lbs_cmd_with_response(priv, CMD_802_11_AD_HOC_STOP, &cmd);

	/* Clean up everything even if there was an error */
	lbs_mac_event_disconnected(priv);

	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}
#endif

static inline int match_bss_no_security(struct lbs_802_11_security *secinfo,
					struct bss_descriptor *match_bss)
{
	if (!secinfo->wep_enabled  && !secinfo->WPAenabled
	    && !secinfo->WPA2enabled
	    && match_bss->wpa_ie[0] != WLAN_EID_GENERIC
	    && match_bss->rsn_ie[0] != WLAN_EID_RSN
	    && !(match_bss->capability & WLAN_CAPABILITY_PRIVACY))
		return 1;
	else
		return 0;
}

static inline int match_bss_static_wep(struct lbs_802_11_security *secinfo,
				       struct bss_descriptor *match_bss)
{
	if (secinfo->wep_enabled && !secinfo->WPAenabled
	    && !secinfo->WPA2enabled
	    && (match_bss->capability & WLAN_CAPABILITY_PRIVACY))
		return 1;
	else
		return 0;
}

static inline int match_bss_wpa(struct lbs_802_11_security *secinfo,
				struct bss_descriptor *match_bss)
{
	if (!secinfo->wep_enabled && secinfo->WPAenabled
	    && (match_bss->wpa_ie[0] == WLAN_EID_GENERIC)
	    /* privacy bit may NOT be set in some APs like LinkSys WRT54G
	    && (match_bss->capability & WLAN_CAPABILITY_PRIVACY) */
	   )
		return 1;
	else
		return 0;
}

static inline int match_bss_wpa2(struct lbs_802_11_security *secinfo,
				 struct bss_descriptor *match_bss)
{
	if (!secinfo->wep_enabled && secinfo->WPA2enabled &&
	    (match_bss->rsn_ie[0] == WLAN_EID_RSN)
	    /* privacy bit may NOT be set in some APs like LinkSys WRT54G
	    (match_bss->capability & WLAN_CAPABILITY_PRIVACY) */
	   )
		return 1;
	else
		return 0;
}

static inline int match_bss_dynamic_wep(struct lbs_802_11_security *secinfo,
					struct bss_descriptor *match_bss)
{
	if (!secinfo->wep_enabled && !secinfo->WPAenabled
	    && !secinfo->WPA2enabled
	    && (match_bss->wpa_ie[0] != WLAN_EID_GENERIC)
	    && (match_bss->rsn_ie[0] != WLAN_EID_RSN)
	    && (match_bss->capability & WLAN_CAPABILITY_PRIVACY))
		return 1;
	else
		return 0;
}

/**
 *  @brief Check if a scanned network compatible with the driver settings
 *
 *   WEP     WPA     WPA2    ad-hoc  encrypt                      Network
 * enabled enabled  enabled   AES     mode   privacy  WPA  WPA2  Compatible
 *    0       0        0       0      NONE      0      0    0   yes No security
 *    1       0        0       0      NONE      1      0    0   yes Static WEP
 *    0       1        0       0       x        1x     1    x   yes WPA
 *    0       0        1       0       x        1x     x    1   yes WPA2
 *    0       0        0       1      NONE      1      0    0   yes Ad-hoc AES
 *    0       0        0       0     !=NONE     1      0    0   yes Dynamic WEP
 *
 *
 *  @param priv A pointer to struct lbs_private
 *  @param index   Index in scantable to check against current driver settings
 *  @param mode    Network mode: Infrastructure or IBSS
 *
 *  @return        Index in scantable, or error code if negative
 */
static int is_network_compatible(struct lbs_private *priv,
				 struct bss_descriptor *bss, uint8_t mode)
{
	int matched = 0;

	lbs_deb_enter(LBS_DEB_SCAN);

	if (bss->mode != mode)
		goto done;

	matched = match_bss_no_security(&priv->secinfo, bss);
	if (matched)
		goto done;
	matched = match_bss_static_wep(&priv->secinfo, bss);
	if (matched)
		goto done;
	matched = match_bss_wpa(&priv->secinfo, bss);
	if (matched) {
		lbs_deb_scan("is_network_compatible() WPA: wpa_ie 0x%x "
			     "wpa2_ie 0x%x WEP %s WPA %s WPA2 %s "
			     "privacy 0x%x\n", bss->wpa_ie[0], bss->rsn_ie[0],
			     priv->secinfo.wep_enabled ? "e" : "d",
			     priv->secinfo.WPAenabled ? "e" : "d",
			     priv->secinfo.WPA2enabled ? "e" : "d",
			     (bss->capability & WLAN_CAPABILITY_PRIVACY));
		goto done;
	}
	matched = match_bss_wpa2(&priv->secinfo, bss);
	if (matched) {
		lbs_deb_scan("is_network_compatible() WPA2: wpa_ie 0x%x "
			     "wpa2_ie 0x%x WEP %s WPA %s WPA2 %s "
			     "privacy 0x%x\n", bss->wpa_ie[0], bss->rsn_ie[0],
			     priv->secinfo.wep_enabled ? "e" : "d",
			     priv->secinfo.WPAenabled ? "e" : "d",
			     priv->secinfo.WPA2enabled ? "e" : "d",
			     (bss->capability & WLAN_CAPABILITY_PRIVACY));
		goto done;
	}
	matched = match_bss_dynamic_wep(&priv->secinfo, bss);
	if (matched) {
		lbs_deb_scan("is_network_compatible() dynamic WEP: "
			     "wpa_ie 0x%x wpa2_ie 0x%x privacy 0x%x\n",
			     bss->wpa_ie[0], bss->rsn_ie[0],
			     (bss->capability & WLAN_CAPABILITY_PRIVACY));
		goto done;
	}

	/* bss security settings don't match those configured on card */
	lbs_deb_scan("is_network_compatible() FAILED: wpa_ie 0x%x "
		     "wpa2_ie 0x%x WEP %s WPA %s WPA2 %s privacy 0x%x\n",
		     bss->wpa_ie[0], bss->rsn_ie[0],
		     priv->secinfo.wep_enabled ? "e" : "d",
		     priv->secinfo.WPAenabled ? "e" : "d",
		     priv->secinfo.WPA2enabled ? "e" : "d",
		     (bss->capability & WLAN_CAPABILITY_PRIVACY));

done:
	lbs_deb_leave_args(LBS_DEB_SCAN, "matched: %d", matched);
	return matched;
}

/**
 *  @brief This function finds a specific compatible BSSID in the scan list
 *
 *  Used in association code
 *
 *  @param priv  A pointer to struct lbs_private
 *  @param bssid    BSSID to find in the scan list
 *  @param mode     Network mode: Infrastructure or IBSS
 *
 *  @return         index in BSSID list, or error return code (< 0)
 */
static struct bss_descriptor *lbs_find_bssid_in_list(struct lbs_private *priv,
					      uint8_t *bssid, uint8_t mode)
{
	struct bss_descriptor *iter_bss;
	struct bss_descriptor *found_bss = NULL;

	lbs_deb_enter(LBS_DEB_SCAN);

	if (!bssid)
		goto out;

	lbs_deb_hex(LBS_DEB_SCAN, "looking for", bssid, ETH_ALEN);

	/* Look through the scan table for a compatible match.  The loop will
	 *   continue past a matched bssid that is not compatible in case there
	 *   is an AP with multiple SSIDs assigned to the same BSSID
	 */
	mutex_lock(&priv->lock);
	list_for_each_entry(iter_bss, &priv->network_list, list) {
		if (compare_ether_addr(iter_bss->bssid, bssid))
			continue; /* bssid doesn't match */
		switch (mode) {
		case IW_MODE_INFRA:
		case IW_MODE_ADHOC:
			if (!is_network_compatible(priv, iter_bss, mode))
				break;
			found_bss = iter_bss;
			break;
		default:
			found_bss = iter_bss;
			break;
		}
	}
	mutex_unlock(&priv->lock);

out:
	lbs_deb_leave_args(LBS_DEB_SCAN, "found_bss %p", found_bss);
	return found_bss;
}

/**
 *  @brief This function finds ssid in ssid list.
 *
 *  Used in association code
 *
 *  @param priv  A pointer to struct lbs_private
 *  @param ssid     SSID to find in the list
 *  @param bssid    BSSID to qualify the SSID selection (if provided)
 *  @param mode     Network mode: Infrastructure or IBSS
 *
 *  @return         index in BSSID list
 */
static struct bss_descriptor *lbs_find_ssid_in_list(struct lbs_private *priv,
					     uint8_t *ssid, uint8_t ssid_len,
					     uint8_t *bssid, uint8_t mode,
					     int channel)
{
	u32 bestrssi = 0;
	struct bss_descriptor *iter_bss = NULL;
	struct bss_descriptor *found_bss = NULL;
	struct bss_descriptor *tmp_oldest = NULL;

	lbs_deb_enter(LBS_DEB_SCAN);

	mutex_lock(&priv->lock);

	list_for_each_entry(iter_bss, &priv->network_list, list) {
		if (!tmp_oldest ||
		    (iter_bss->last_scanned < tmp_oldest->last_scanned))
			tmp_oldest = iter_bss;

		if (lbs_ssid_cmp(iter_bss->ssid, iter_bss->ssid_len,
				 ssid, ssid_len) != 0)
			continue; /* ssid doesn't match */
		if (bssid && compare_ether_addr(iter_bss->bssid, bssid) != 0)
			continue; /* bssid doesn't match */
		if ((channel > 0) && (iter_bss->channel != channel))
			continue; /* channel doesn't match */

		switch (mode) {
		case IW_MODE_INFRA:
		case IW_MODE_ADHOC:
			if (!is_network_compatible(priv, iter_bss, mode))
				break;

			if (bssid) {
				/* Found requested BSSID */
				found_bss = iter_bss;
				goto out;
			}

			if (SCAN_RSSI(iter_bss->rssi) > bestrssi) {
				bestrssi = SCAN_RSSI(iter_bss->rssi);
				found_bss = iter_bss;
			}
			break;
		case IW_MODE_AUTO:
		default:
			if (SCAN_RSSI(iter_bss->rssi) > bestrssi) {
				bestrssi = SCAN_RSSI(iter_bss->rssi);
				found_bss = iter_bss;
			}
			break;
		}
	}

out:
	mutex_unlock(&priv->lock);
	lbs_deb_leave_args(LBS_DEB_SCAN, "found_bss %p", found_bss);
	return found_bss;
}

static int assoc_helper_essid(struct lbs_private *priv,
                              struct assoc_request * assoc_req)
{
	int ret = 0;
	struct bss_descriptor * bss;
	int channel = -1;
	DECLARE_SSID_BUF(ssid);

	lbs_deb_enter(LBS_DEB_ASSOC);

	/* FIXME: take channel into account when picking SSIDs if a channel
	 * is set.
	 */

	if (test_bit(ASSOC_FLAG_CHANNEL, &assoc_req->flags))
		channel = assoc_req->channel;

	lbs_deb_assoc
	//printk
		("SSID '%s' requested\n",
	              mv8686_print_ssid(ssid, assoc_req->ssid, assoc_req->ssid_len));
	if (assoc_req->mode == IW_MODE_INFRA) {
		/* Find in list first -- Yongle Lai */
		bss = lbs_find_ssid_in_list(priv, assoc_req->ssid,
				assoc_req->ssid_len, NULL, IW_MODE_INFRA, channel);
		if (bss == NULL) 
		{
			lbs_send_specific_ssid_scan(priv, assoc_req->ssid, assoc_req->ssid_len);
			bss = lbs_find_ssid_in_list(priv, assoc_req->ssid,
				assoc_req->ssid_len, NULL, IW_MODE_INFRA, channel);
		}
		else
			printk("Needn't special scan for this ssid to do association.\n");

		if (bss != NULL) {
			memcpy(&assoc_req->bss, bss, sizeof(struct bss_descriptor));
			ret = lbs_associate(priv, assoc_req);
		} else {
			printk("WIFI-ERROR: SSID <%s> not found; cannot associate\n", assoc_req->ssid);
		}
	} 
	
#if (MV8686_SUPPORT_ADHOC == 1)

	else if (assoc_req->mode == IW_MODE_ADHOC) {
		/* Scan for the network, do not save previous results.  Stale
		 *   scan data will cause us to join a non-existant adhoc network
		 */
		lbs_send_specific_ssid_scan(priv, assoc_req->ssid,
			assoc_req->ssid_len);

		/* Search for the requested SSID in the scan table */
		bss = lbs_find_ssid_in_list(priv, assoc_req->ssid,
				assoc_req->ssid_len, NULL, IW_MODE_ADHOC, channel);
		if (bss != NULL) {
			lbs_deb_assoc("SSID found, will join\n");
			memcpy(&assoc_req->bss, bss, sizeof(struct bss_descriptor));
			lbs_adhoc_join(priv, assoc_req);
		} else {
			/* else send START command */
			lbs_deb_assoc("SSID not found, creating adhoc network\n");
			memcpy(&assoc_req->bss.ssid, &assoc_req->ssid,
				IW_ESSID_MAX_SIZE);
			assoc_req->bss.ssid_len = assoc_req->ssid_len;
			lbs_adhoc_start(priv, assoc_req);
		}
	}
#endif
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}


static int assoc_helper_bssid(struct lbs_private *priv,
                              struct assoc_request * assoc_req)
{
	int ret = 0;
	struct bss_descriptor * bss;

	lbs_deb_enter_args(LBS_DEB_ASSOC, "BSSID %pM", assoc_req->bssid);

	/* Search for index position in list for requested MAC */
	bss = lbs_find_bssid_in_list(priv, assoc_req->bssid,
			    assoc_req->mode);
	if (bss == NULL) {
		lbs_deb_assoc("ASSOC: WAP: BSSID %pM not found, "
			"cannot associate.\n", assoc_req->bssid);
		goto out;
	}

	memcpy(&assoc_req->bss, bss, sizeof(struct bss_descriptor));
	if (assoc_req->mode == IW_MODE_INFRA) {
		ret = lbs_associate(priv, assoc_req);
		lbs_deb_assoc("ASSOC: lbs_associate(bssid) returned %d\n", ret);
	}
#if (MV8686_SUPPORT_ADHOC == 1)

	else if (assoc_req->mode == IW_MODE_ADHOC) {
		lbs_adhoc_join(priv, assoc_req);
	}
#endif

out:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}


static int assoc_helper_associate(struct lbs_private *priv,
                                  struct assoc_request * assoc_req)
{
	int ret = 0, done = 0;

	lbs_deb_enter(LBS_DEB_ASSOC);

	/* If we're given and 'any' BSSID, try associating based on SSID */

	if (test_bit(ASSOC_FLAG_BSSID, &assoc_req->flags)) {
		if (compare_ether_addr(bssid_any, assoc_req->bssid)
		    && compare_ether_addr(bssid_off, assoc_req->bssid)) {
			ret = assoc_helper_bssid(priv, assoc_req);
			done = 1;
		}
	}

	if (!done && test_bit(ASSOC_FLAG_SSID, &assoc_req->flags)) {
		ret = assoc_helper_essid(priv, assoc_req);
	}

	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}

#if (NEW_MV8686_PS == 1)
static int assoc_helper_mode(struct lbs_private *priv,
                             struct assoc_request * assoc_req)
{
	int ret = 0;

	lbs_deb_enter(LBS_DEB_ASSOC);

	if (assoc_req->mode == priv->mode)
		goto done;

	if (assoc_req->mode == IW_MODE_INFRA) {
		if (priv->psstate != PS_STATE_FULL_POWER)
			lbs_ps_wakeup(priv, CMD_OPTION_WAITFORRSP);
		priv->psmode = LBS802_11POWERMODECAM;
	}

	priv->mode = assoc_req->mode;
	ret = lbs_set_snmp_mib(priv, SNMP_MIB_OID_BSS_TYPE, assoc_req->mode);

done:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}
#endif

static int assoc_helper_channel(struct lbs_private *priv,
                                struct assoc_request * assoc_req)
{
	int ret = 0;

	lbs_deb_enter(LBS_DEB_ASSOC);

	ret = lbs_update_channel(priv);
	if (ret) {
		lbs_deb_assoc("ASSOC: channel: error getting channel.\n");
		goto done;
	}

	if (assoc_req->channel == priv->curbssparams.channel)
		goto done;

	lbs_deb_assoc("ASSOC: channel: %d -> %d\n",
		      priv->curbssparams.channel, assoc_req->channel);

	ret = lbs_set_channel(priv, assoc_req->channel);
	if (ret < 0)
		lbs_deb_assoc("ASSOC: channel: error setting channel.\n");

	/* FIXME: shouldn't need to grab the channel _again_ after setting
	 * it since the firmware is supposed to return the new channel, but
	 * whatever... */
	ret = lbs_update_channel(priv);
	if (ret) {
		lbs_deb_assoc("ASSOC: channel: error getting channel.\n");
		goto done;
	}

	if (assoc_req->channel != priv->curbssparams.channel) {
		lbs_deb_assoc("ASSOC: channel: failed to update channel to %d\n",
		              assoc_req->channel);
		goto restore_mesh;
	}

	if (   assoc_req->secinfo.wep_enabled
	    &&   (assoc_req->wep_keys[0].len
	       || assoc_req->wep_keys[1].len
	       || assoc_req->wep_keys[2].len
	       || assoc_req->wep_keys[3].len)) {
		/* Make sure WEP keys are re-sent to firmware */
		set_bit(ASSOC_FLAG_WEP_KEYS, &assoc_req->flags);
	}

	/* Must restart/rejoin adhoc networks after channel change */
 	set_bit(ASSOC_FLAG_SSID, &assoc_req->flags);

 restore_mesh:
 done:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}


static int assoc_helper_wep_keys(struct lbs_private *priv,
				 struct assoc_request *assoc_req)
{
	int i;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_ASSOC);

	/* Set or remove WEP keys */
	if (assoc_req->wep_keys[0].len || assoc_req->wep_keys[1].len ||
	    assoc_req->wep_keys[2].len || assoc_req->wep_keys[3].len)
		ret = lbs_cmd_802_11_set_wep(priv, CMD_ACT_ADD, assoc_req);
	else
		ret = lbs_cmd_802_11_set_wep(priv, CMD_ACT_REMOVE, assoc_req);

	if (ret)
		goto out;

	/* enable/disable the MAC's WEP packet filter */
	if (assoc_req->secinfo.wep_enabled)
		priv->mac_control |= CMD_ACT_MAC_WEP_ENABLE;
	else
		priv->mac_control &= ~CMD_ACT_MAC_WEP_ENABLE;

	lbs_set_mac_control(priv);

	mutex_lock(&priv->lock);

	/* Copy WEP keys into priv wep key fields */
	for (i = 0; i < 4; i++) {
		memcpy(&priv->wep_keys[i], &assoc_req->wep_keys[i],
		       sizeof(struct enc_key));
	}
	priv->wep_tx_keyidx = assoc_req->wep_tx_keyidx;

	mutex_unlock(&priv->lock);

out:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}

static int assoc_helper_secinfo(struct lbs_private *priv,
                                struct assoc_request * assoc_req)
{
	int ret = 0;
	uint16_t do_wpa;
	uint16_t rsn = 0;

	lbs_deb_enter(LBS_DEB_ASSOC);

	memcpy(&priv->secinfo, &assoc_req->secinfo,
		sizeof(struct lbs_802_11_security));

	lbs_set_mac_control(priv);

	/* If RSN is already enabled, don't try to enable it again, since
	 * ENABLE_RSN resets internal state machines and will clobber the
	 * 4-way WPA handshake.
	 */

	/* Get RSN enabled/disabled */
	ret = lbs_cmd_802_11_enable_rsn(priv, CMD_ACT_GET, &rsn);
	if (ret) {
		lbs_deb_assoc("Failed to get RSN status: %d\n", ret);
		goto out;
	}

	/* Don't re-enable RSN if it's already enabled */
	do_wpa = assoc_req->secinfo.WPAenabled || assoc_req->secinfo.WPA2enabled;
	if (do_wpa == rsn)
		goto out;

	/* Set RSN enabled/disabled */
	ret = lbs_cmd_802_11_enable_rsn(priv, CMD_ACT_SET, &do_wpa);

out:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}


static int assoc_helper_wpa_keys(struct lbs_private *priv,
                                 struct assoc_request * assoc_req)
{
	int ret = 0;
	unsigned int flags = assoc_req->flags;

	lbs_deb_enter(LBS_DEB_ASSOC);

	/* Work around older firmware bug where WPA unicast and multicast
	 * keys must be set independently.  Seen in SDIO parts with firmware
	 * version 5.0.11p0.
	 */

	if (test_bit(ASSOC_FLAG_WPA_UCAST_KEY, &assoc_req->flags)) {
		clear_bit(ASSOC_FLAG_WPA_MCAST_KEY, &assoc_req->flags);
		ret = lbs_cmd_802_11_key_material(priv, CMD_ACT_SET, assoc_req);
		assoc_req->flags = flags;
	}

	if (ret)
		goto out;

	if (test_bit(ASSOC_FLAG_WPA_MCAST_KEY, &assoc_req->flags)) {
		clear_bit(ASSOC_FLAG_WPA_UCAST_KEY, &assoc_req->flags);

		ret = lbs_cmd_802_11_key_material(priv, CMD_ACT_SET, assoc_req);
		assoc_req->flags = flags;
	}

out:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}


static int assoc_helper_wpa_ie(struct lbs_private *priv,
                               struct assoc_request * assoc_req)
{
	int ret = 0;

	lbs_deb_enter(LBS_DEB_ASSOC);

	if (assoc_req->secinfo.WPAenabled || assoc_req->secinfo.WPA2enabled) {
		memcpy(&priv->wpa_ie, &assoc_req->wpa_ie, assoc_req->wpa_ie_len);
		priv->wpa_ie_len = assoc_req->wpa_ie_len;
	} else {
		memset(&priv->wpa_ie, 0, MAX_WPA_IE_LEN);
		priv->wpa_ie_len = 0;
	}

	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}


static int should_deauth_infrastructure(struct lbs_private *priv,
                                        struct assoc_request * assoc_req)
{
	int ret = 0;

	if (priv->connect_status != LBS_CONNECTED)
		return 0;

	lbs_deb_enter(LBS_DEB_ASSOC);
	if (test_bit(ASSOC_FLAG_SSID, &assoc_req->flags)) {
		printk("Deauthenticating due to new SSID\n");
		ret = 1;
		goto out;
	}

	if (test_bit(ASSOC_FLAG_SECINFO, &assoc_req->flags)) {
		if (priv->secinfo.auth_mode != assoc_req->secinfo.auth_mode) {
			printk("Deauthenticating due to new security\n");
			ret = 1;
			goto out;
		}
	}

	if (test_bit(ASSOC_FLAG_BSSID, &assoc_req->flags)) {
		printk("Deauthenticating due to new BSSID\n");
		ret = 1;
		goto out;
	}

	if (test_bit(ASSOC_FLAG_CHANNEL, &assoc_req->flags)) {
		printk("Deauthenticating due to channel switch\n");
		ret = 1;
		goto out;
	}

#if (NEW_MV8686_PS == 1)
	/* FIXME: deal with 'auto' mode somehow */
	if (test_bit(ASSOC_FLAG_MODE, &assoc_req->flags)) {
		if (assoc_req->mode != IW_MODE_INFRA) {
			lbs_deb_assoc("Deauthenticating due to leaving "
				"infra mode\n");
			ret = 1;
			goto out;
		}
	}
#endif

out:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}

#if (MV8686_SUPPORT_ADHOC == 1)
static int should_stop_adhoc(struct lbs_private *priv,
                             struct assoc_request * assoc_req)
{
	lbs_deb_enter(LBS_DEB_ASSOC);

	if (priv->connect_status != LBS_CONNECTED)
		return 0;

	if (lbs_ssid_cmp(priv->curbssparams.ssid,
	                      priv->curbssparams.ssid_len,
	                      assoc_req->ssid, assoc_req->ssid_len) != 0)
		return 1;

	/* FIXME: deal with 'auto' mode somehow */
	if (test_bit(ASSOC_FLAG_MODE, &assoc_req->flags)) {
		if (assoc_req->mode != IW_MODE_ADHOC)
			return 1;
	}

	if (test_bit(ASSOC_FLAG_CHANNEL, &assoc_req->flags)) {
		if (assoc_req->channel != priv->curbssparams.channel)
			return 1;
	}

	lbs_deb_leave(LBS_DEB_ASSOC);
	return 0;
}
#endif

/**
 *  @brief This function finds the best SSID in the Scan List
 *
 *  Search the scan table for the best SSID that also matches the current
 *   adapter network preference (infrastructure or adhoc)
 *
 *  @param priv  A pointer to struct lbs_private
 *
 *  @return         index in BSSID list
 */
static struct bss_descriptor *lbs_find_best_ssid_in_list(
	struct lbs_private *priv, uint8_t mode)
{
	uint8_t bestrssi = 0;
	struct bss_descriptor *iter_bss;
	struct bss_descriptor *best_bss = NULL;

	lbs_deb_enter(LBS_DEB_SCAN);

	mutex_lock(&priv->lock);

	list_for_each_entry(iter_bss, &priv->network_list, list) {
		switch (mode) {
		case IW_MODE_INFRA:
		case IW_MODE_ADHOC:
			if (!is_network_compatible(priv, iter_bss, mode))
				break;
			if (SCAN_RSSI(iter_bss->rssi) <= bestrssi)
				break;
			bestrssi = SCAN_RSSI(iter_bss->rssi);
			best_bss = iter_bss;
			break;
		case IW_MODE_AUTO:
		default:
			if (SCAN_RSSI(iter_bss->rssi) <= bestrssi)
				break;
			bestrssi = SCAN_RSSI(iter_bss->rssi);
			best_bss = iter_bss;
			break;
		}
	}

	mutex_unlock(&priv->lock);
	lbs_deb_leave_args(LBS_DEB_SCAN, "best_bss %p", best_bss);
	return best_bss;
}

/**
 *  @brief Find the best AP
 *
 *  Used from association worker.
 *
 *  @param priv         A pointer to struct lbs_private structure
 *  @param pSSID        A pointer to AP's ssid
 *
 *  @return             0--success, otherwise--fail
 */
static int lbs_find_best_network_ssid(struct lbs_private *priv,
	uint8_t *out_ssid, uint8_t *out_ssid_len, uint8_t preferred_mode,
	uint8_t *out_mode)
{
	int ret = -1;
	struct bss_descriptor *found;

	lbs_deb_enter(LBS_DEB_SCAN);

	priv->scan_ssid_len = 0;
	lbs_scan_networks(priv, 1);
	if (priv->surpriseremoved)
		goto out;

	found = lbs_find_best_ssid_in_list(priv, preferred_mode);
	if (found && (found->ssid_len > 0)) {
		memcpy(out_ssid, &found->ssid, IW_ESSID_MAX_SIZE);
		*out_ssid_len = found->ssid_len;
		*out_mode = found->mode;
		ret = 0;
	}

out:
	lbs_deb_leave_args(LBS_DEB_SCAN, "ret %d", ret);
	return ret;
}

int wep_assoc_retry = 0;

void lbs_association_worker(struct work_struct *work)
{
	struct lbs_private *priv = container_of(work, struct lbs_private,
		assoc_work.work);
	struct assoc_request * assoc_req = NULL;
	int ret = 0;
	int find_any_ssid = 0;
	DECLARE_SSID_BUF(ssid);

	lbs_deb_enter(LBS_DEB_ASSOC);

tryagain:
	//printk("lbs_association_worker\n");
	
	mutex_lock(&priv->lock);
	assoc_req = priv->pending_assoc_req;
	priv->pending_assoc_req = NULL;
	priv->in_progress_assoc_req = assoc_req;
	mutex_unlock(&priv->lock);

	if (!assoc_req)
		goto done;

	lbs_deb_assoc(
	//printk(
		"Association Request:\n"
		"    flags:     0x%08lx\n"
		"    SSID:      '%s'\n"
		"    chann:     %d\n"
		"    band:      %d\n"
		"    mode:      %d\n"
		"    BSSID:     %pM\n"
		"    secinfo:  %s%s%s\n"
		"    auth_mode: %d\n",
		assoc_req->flags,
		mv8686_print_ssid(ssid, assoc_req->ssid, assoc_req->ssid_len),
		assoc_req->channel, assoc_req->band, assoc_req->mode,
		assoc_req->bssid,
		assoc_req->secinfo.WPAenabled ? " WPA" : "",
		assoc_req->secinfo.WPA2enabled ? " WPA2" : "",
		assoc_req->secinfo.wep_enabled ? " WEP" : "",
		assoc_req->secinfo.auth_mode);

	/* If 'any' SSID was specified, find an SSID to associate with */
	if (test_bit(ASSOC_FLAG_SSID, &assoc_req->flags) && !assoc_req->ssid_len)
		find_any_ssid = 1;

	/* But don't use 'any' SSID if there's a valid locked BSSID to use */
	if (test_bit(ASSOC_FLAG_BSSID, &assoc_req->flags)) 
	{
		if (compare_ether_addr(assoc_req->bssid, bssid_any) && 
			compare_ether_addr(assoc_req->bssid, bssid_off))
			find_any_ssid = 0;
	}

	if (find_any_ssid) 
	{
		u8 new_mode = assoc_req->mode;

		ret = lbs_find_best_network_ssid(priv, assoc_req->ssid,
				&assoc_req->ssid_len, assoc_req->mode, &new_mode);
		if (ret) {
			lbs_deb_assoc("Could not find best network\n");
			ret = -ENETUNREACH;
			goto out;
		}

		/* Ensure we switch to the mode of the AP */
		if (assoc_req->mode == IW_MODE_AUTO) 
		{
			set_bit(ASSOC_FLAG_MODE, &assoc_req->flags);
			assoc_req->mode = new_mode;
		}
	}

	/*
	 * Check if the attributes being changing require deauthentication
	 * from the currently associated infrastructure access point.
	 */
	if (priv->mode == IW_MODE_INFRA) 
	{
		if (should_deauth_infrastructure(priv, assoc_req)) {
			ret = lbs_cmd_80211_deauthenticate(priv,
							   priv->curbssparams.bssid,
							   WLAN_REASON_DEAUTH_LEAVING);
			if (ret) {
				lbs_deb_assoc("Deauthentication due to new "
					"configuration request failed: %d\n",
					ret);
			}
		}
	}
#if (MV8686_SUPPORT_ADHOC == 1)

	else if (priv->mode == IW_MODE_ADHOC) {
		if (should_stop_adhoc(priv, assoc_req)) {
			ret = lbs_adhoc_stop(priv);
			if (ret) {
				lbs_deb_assoc("Teardown of AdHoc network due to "
					"new configuration request failed: %d\n",
					ret);
			}

		}
	}
#endif

#if (NEW_MV8686_PS == 0)
	assoc_req->mode = priv->mode;
#else
	/* Send the various configuration bits to the firmware */
	if (test_bit(ASSOC_FLAG_MODE, &assoc_req->flags)) {
		ret = assoc_helper_mode(priv, assoc_req);
		if (ret)
			goto out;
	}
#endif

	if (test_bit(ASSOC_FLAG_CHANNEL, &assoc_req->flags)) {
		ret = assoc_helper_channel(priv, assoc_req);
		if (ret)
			goto out;
	}

	if (   test_bit(ASSOC_FLAG_WEP_KEYS, &assoc_req->flags)
	    || test_bit(ASSOC_FLAG_WEP_TX_KEYIDX, &assoc_req->flags)) {
		ret = assoc_helper_wep_keys(priv, assoc_req);
		if (ret)
			goto out;
	}

	if (test_bit(ASSOC_FLAG_SECINFO, &assoc_req->flags)) {
		ret = assoc_helper_secinfo(priv, assoc_req);
		if (ret)
			goto out;
	}

	if (test_bit(ASSOC_FLAG_WPA_IE, &assoc_req->flags)) {
		ret = assoc_helper_wpa_ie(priv, assoc_req);
		if (ret)
			goto out;
	}

	if (test_bit(ASSOC_FLAG_WPA_MCAST_KEY, &assoc_req->flags)
	    || test_bit(ASSOC_FLAG_WPA_UCAST_KEY, &assoc_req->flags)) {
		ret = assoc_helper_wpa_keys(priv, assoc_req);
		if (ret)
			goto out;
	}

	/* 
	 * SSID/BSSID should be the _last_ config option set, because they
	 * trigger the association attempt.
	 */
	if (test_bit(ASSOC_FLAG_BSSID, &assoc_req->flags)
	    || test_bit(ASSOC_FLAG_SSID, &assoc_req->flags)) {
		int success = 1;

#if 0
		if (test_bit(ASSOC_FLAG_BSSID, &assoc_req->flags))
			printk("%s: Trigger association due to BSSID FLAG.\n", __func__);
		else
			printk("%s: Trigger association due to ESSID FLAG.\n", __func__);
#endif

		ret = assoc_helper_associate(priv, assoc_req);
		if (ret) {
			lbs_deb_assoc("ASSOC: association unsuccessful: %d\n",
				ret);
			success = 0;
		}

		if (priv->connect_status != LBS_CONNECTED) {
			lbs_deb_assoc("ASSOC: association unsuccessful, not connected\n");
			success = 0;
		}

		if (success) 
		{
			lbs_deb_assoc("associated to %pM\n", priv->curbssparams.bssid);
			lbs_prepare_and_send_command(priv, CMD_802_11_RSSI, 0, 
			                             CMD_OPTION_WAITFORRSP, 0, NULL);
#if (AUTO_REASSOC == 1)
			if (priv->prev_assoc_req && priv->in_progress_assoc_req)
				memcpy(priv->prev_assoc_req, priv->in_progress_assoc_req, 
			           sizeof(struct assoc_request));
#endif
		} else {
			ret = -1;
		}
	}

out:
	if (ret) {
		lbs_deb_assoc("ASSOC: reconfiguration attempt unsuccessful: %d\n",
			ret);
	}

	mutex_lock(&priv->lock);
	if (wep_assoc_retry == 1)
	{
		//printk("We are going to try association again.\n");
		wep_assoc_retry = 0;
		mutex_unlock(&priv->lock);
		goto tryagain;
	}
	priv->in_progress_assoc_req = NULL;
	mutex_unlock(&priv->lock);
	kfree(assoc_req);

done:
	atomic_set(&reassoc_flag, 0);
	lbs_deb_leave(LBS_DEB_ASSOC);
}


/*
 * Caller MUST hold any necessary locks
 */
struct assoc_request *lbs_get_association_request(struct lbs_private *priv)
{
	struct assoc_request * assoc_req;

	lbs_deb_enter(LBS_DEB_ASSOC);
	if (!priv->pending_assoc_req) {
		priv->pending_assoc_req = kzalloc(sizeof(struct assoc_request),
		                                     GFP_KERNEL);
		if (!priv->pending_assoc_req) {
			lbs_pr_info("Not enough memory to allocate association"
				" request!\n");
			return NULL;
		}
	}

	/* Copy current configuration attributes to the association request,
	 * but don't overwrite any that are already set.
	 */
	assoc_req = priv->pending_assoc_req;
	if (!test_bit(ASSOC_FLAG_SSID, &assoc_req->flags)) {
		memcpy(&assoc_req->ssid, &priv->curbssparams.ssid,
		       IW_ESSID_MAX_SIZE);
		assoc_req->ssid_len = priv->curbssparams.ssid_len;
	}

	if (!test_bit(ASSOC_FLAG_CHANNEL, &assoc_req->flags))
		assoc_req->channel = priv->curbssparams.channel;

	if (!test_bit(ASSOC_FLAG_BAND, &assoc_req->flags))
		assoc_req->band = priv->curbssparams.band;

	if (!test_bit(ASSOC_FLAG_MODE, &assoc_req->flags))
		assoc_req->mode = priv->mode;

	if (!test_bit(ASSOC_FLAG_BSSID, &assoc_req->flags)) {
		memcpy(&assoc_req->bssid, priv->curbssparams.bssid,
			ETH_ALEN);
	}

	if (!test_bit(ASSOC_FLAG_WEP_KEYS, &assoc_req->flags)) {
		int i;
		for (i = 0; i < 4; i++) {
			memcpy(&assoc_req->wep_keys[i], &priv->wep_keys[i],
				sizeof(struct enc_key));
		}
	}

	if (!test_bit(ASSOC_FLAG_WEP_TX_KEYIDX, &assoc_req->flags))
		assoc_req->wep_tx_keyidx = priv->wep_tx_keyidx;

	if (!test_bit(ASSOC_FLAG_WPA_MCAST_KEY, &assoc_req->flags)) {
		memcpy(&assoc_req->wpa_mcast_key, &priv->wpa_mcast_key,
			sizeof(struct enc_key));
	}

	if (!test_bit(ASSOC_FLAG_WPA_UCAST_KEY, &assoc_req->flags)) {
		memcpy(&assoc_req->wpa_unicast_key, &priv->wpa_unicast_key,
			sizeof(struct enc_key));
	}

	if (!test_bit(ASSOC_FLAG_SECINFO, &assoc_req->flags)) {
		memcpy(&assoc_req->secinfo, &priv->secinfo,
			sizeof(struct lbs_802_11_security));
		//printk("111assoc_req->secinfo.auth_mode=%d\n", assoc_req->secinfo.auth_mode);
	}

	if (!test_bit(ASSOC_FLAG_WPA_IE, &assoc_req->flags)) {
		memcpy(&assoc_req->wpa_ie, &priv->wpa_ie,
			MAX_WPA_IE_LEN);
		assoc_req->wpa_ie_len = priv->wpa_ie_len;
	}

	lbs_deb_leave(LBS_DEB_ASSOC);
	return assoc_req;
}


/**
 *  @brief This function prepares command of authenticate.
 *
 *  @param priv      A pointer to struct lbs_private structure
 *  @param cmd       A pointer to cmd_ds_command structure
 *  @param pdata_buf Void cast of pointer to a BSSID to authenticate with
 *
 *  @return         0 or -1
 */
int lbs_cmd_80211_authenticate(struct lbs_private *priv,
				 struct cmd_ds_command *cmd,
				 void *pdata_buf)
{
	struct cmd_ds_802_11_authenticate *pauthenticate = &cmd->params.auth;
	int ret = -1;
	u8 *bssid = pdata_buf;

	lbs_deb_enter(LBS_DEB_JOIN);

	cmd->command = cpu_to_le16(CMD_802_11_AUTHENTICATE);
	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_802_11_authenticate)
			+ S_DS_GEN);

	/* translate auth mode to 802.11 defined wire value */
	switch (priv->secinfo.auth_mode) {
	case IW_AUTH_ALG_OPEN_SYSTEM:
		pauthenticate->authtype = 0x00;
		break;
	case IW_AUTH_ALG_SHARED_KEY:
		pauthenticate->authtype = 0x01;
		break;
	case IW_AUTH_ALG_LEAP:
		pauthenticate->authtype = 0x80;
		break;
	default:
		lbs_deb_join("AUTH_CMD: invalid auth alg 0x%X\n",
			priv->secinfo.auth_mode);
		goto out;
	}

	memcpy(pauthenticate->macaddr, bssid, ETH_ALEN);

	lbs_deb_join("AUTH_CMD: BSSID %pM, auth 0x%x\n",
		bssid, pauthenticate->authtype);
	ret = 0;

out:
	lbs_deb_leave_args(LBS_DEB_JOIN, "ret %d", ret);
	return ret;
}

/**
 *  @brief Deauthenticate from a specific BSS
 *
 *  @param priv        A pointer to struct lbs_private structure
 *  @param bssid       The specific BSS to deauthenticate from
 *  @param reason      The 802.11 sec. 7.3.1.7 Reason Code for deauthenticating
 *
 *  @return            0 on success, error on failure
 */
int lbs_cmd_80211_deauthenticate(struct lbs_private *priv, u8 bssid[ETH_ALEN],
				 u16 reason)
{
	struct cmd_ds_802_11_deauthenticate cmd;
	int ret;

	printk("%s: enter...\n", __func__);
	
	lbs_deb_enter(LBS_DEB_JOIN);

	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	memcpy(cmd.macaddr, &bssid[0], ETH_ALEN);
	cmd.reasoncode = cpu_to_le16(reason);

	ret = lbs_cmd_with_response(priv, CMD_802_11_DEAUTHENTICATE, &cmd);

	/* 
	 * Clean up everything even if there was an error; can't assume that
	 * we're still authenticated to the AP after trying to deauth.
	 */
	if ((atomic_read(&reassoc_flag) == 1) ||
		(priv->connect_status == LBS_DISCONNECTED))
	{
		printk("Needn't send disconnected event for association.\n");
		return ret;
	}
	else
		lbs_mac_event_disconnected(priv);

	lbs_deb_leave(LBS_DEB_JOIN);
	
	return ret;
}

int lbs_cmd_80211_associate(struct lbs_private *priv,
			      struct cmd_ds_command *cmd, void *pdata_buf)
{
	struct cmd_ds_802_11_associate *passo = &cmd->params.associate;
	int ret = 0;
	struct assoc_request *assoc_req = pdata_buf;
	struct bss_descriptor *bss = &assoc_req->bss;
	u8 *pos;
	u16 tmpcap, tmplen;
	struct mrvlietypes_ssidparamset *ssid;
	struct mrvlietypes_phyparamset *phy;
	struct mrvlietypes_ssparamset *ss;
	struct mrvlietypes_ratesparamset *rates;
	struct mrvlietypes_rsnparamset *rsn;

	lbs_deb_enter(LBS_DEB_ASSOC);

	pos = (u8 *) passo;

	if (!priv) {
		ret = -1;
		goto done;
	}

	cmd->command = cpu_to_le16(CMD_802_11_ASSOCIATE);

	memcpy(passo->peerstaaddr, bss->bssid, sizeof(passo->peerstaaddr));
	pos += sizeof(passo->peerstaaddr);

	/* set the listen interval */
	passo->listeninterval = cpu_to_le16(MRVDRV_DEFAULT_LISTEN_INTERVAL);
	
	pos += sizeof(passo->capability);
	pos += sizeof(passo->listeninterval);
	pos += sizeof(passo->bcnperiod);
	pos += sizeof(passo->dtimperiod);

	ssid = (struct mrvlietypes_ssidparamset *) pos;
	ssid->header.type = cpu_to_le16(TLV_TYPE_SSID);
	tmplen = bss->ssid_len;
	ssid->header.len = cpu_to_le16(tmplen);
	memcpy(ssid->ssid, bss->ssid, tmplen);
	pos += sizeof(ssid->header) + tmplen;

	phy = (struct mrvlietypes_phyparamset *) pos;
	phy->header.type = cpu_to_le16(TLV_TYPE_PHY_DS);
	tmplen = sizeof(phy->fh_ds.dsparamset);
	phy->header.len = cpu_to_le16(tmplen);
	memcpy(&phy->fh_ds.dsparamset,
	       &bss->phyparamset.dsparamset.currentchan,
	       tmplen);
	pos += sizeof(phy->header) + tmplen;

	ss = (struct mrvlietypes_ssparamset *) pos;
	ss->header.type = cpu_to_le16(TLV_TYPE_CF);
	tmplen = sizeof(ss->cf_ibss.cfparamset);
	ss->header.len = cpu_to_le16(tmplen);
	pos += sizeof(ss->header) + tmplen;

	rates = (struct mrvlietypes_ratesparamset *) pos;
	rates->header.type = cpu_to_le16(TLV_TYPE_RATES);
	memcpy(&rates->rates, &bss->rates, MAX_RATES);
	tmplen = MAX_RATES;
	if (get_common_rates(priv, rates->rates, &tmplen)) {
		ret = -1;
		goto done;
	}
	pos += sizeof(rates->header) + tmplen;
	rates->header.len = cpu_to_le16(tmplen);
	lbs_deb_assoc("ASSOC_CMD: num rates %u\n", tmplen);

	/* Copy the infra. association rates into Current BSS state structure */
	memset(&priv->curbssparams.rates, 0, sizeof(priv->curbssparams.rates));
	memcpy(&priv->curbssparams.rates, &rates->rates, tmplen);

	/* Set MSB on basic rates as the firmware requires, but _after_
	 * copying to current bss rates.
	 */
	lbs_set_basic_rate_flags(rates->rates, tmplen);

	if (assoc_req->secinfo.WPAenabled || assoc_req->secinfo.WPA2enabled) {
		rsn = (struct mrvlietypes_rsnparamset *) pos;
		/* WPA_IE or WPA2_IE */
		rsn->header.type = cpu_to_le16((u16) assoc_req->wpa_ie[0]);
		tmplen = (u16) assoc_req->wpa_ie[1];
		rsn->header.len = cpu_to_le16(tmplen);
		memcpy(rsn->rsnie, &assoc_req->wpa_ie[2], tmplen);
		lbs_deb_hex(LBS_DEB_JOIN, "ASSOC_CMD: RSN IE", (u8 *) rsn,
			sizeof(rsn->header) + tmplen);
		pos += sizeof(rsn->header) + tmplen;
	}

	/* update curbssparams */
	priv->curbssparams.channel = bss->phyparamset.dsparamset.currentchan;

	if (lbs_parse_dnld_countryinfo_11d(priv, bss)) {
		ret = -1;
		goto done;
	}

	cmd->size = cpu_to_le16((u16) (pos - (u8 *) passo) + S_DS_GEN);

	/* set the capability info */
	tmpcap = (bss->capability & CAPINFO_MASK);
	if (bss->mode == IW_MODE_INFRA)
		tmpcap |= WLAN_CAPABILITY_ESS;
	passo->capability = cpu_to_le16(tmpcap);
	lbs_deb_assoc("ASSOC_CMD: capability 0x%04x\n", tmpcap);

done:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}

static inline void lbs_postpone_association_work2(struct lbs_private *priv)
{
	//if (priv->surpriseremoved)
	//	return;
	//cancel_delayed_work(&priv->assoc_work);
	queue_delayed_work(priv->work_thread, &priv->assoc_work, HZ / 2);
}

int lbs_ret_80211_associate(struct lbs_private *priv,
			      struct cmd_ds_command *resp)
{
	int ret = 0;
	union iwreq_data wrqu;
	struct ieeetypes_assocrsp *passocrsp;
	struct bss_descriptor *bss;
	u16 status_code;

	lbs_deb_enter(LBS_DEB_ASSOC);

	if (!priv->in_progress_assoc_req) {
		lbs_deb_assoc("ASSOC_RESP: no in-progress assoc request\n");
		ret = -1;
		goto done;
	}
	bss = &priv->in_progress_assoc_req->bss;

	passocrsp = (struct ieeetypes_assocrsp *) &resp->params;

	/*
	 * Older FW versions map the IEEE 802.11 Status Code in the association
	 * response to the following values returned in passocrsp->statuscode:
	 *
	 *    IEEE Status Code                Marvell Status Code
	 *    0                       ->      0x0000 ASSOC_RESULT_SUCCESS
	 *    13                      ->      0x0004 ASSOC_RESULT_AUTH_REFUSED
	 *    14                      ->      0x0004 ASSOC_RESULT_AUTH_REFUSED
	 *    15                      ->      0x0004 ASSOC_RESULT_AUTH_REFUSED
	 *    16                      ->      0x0004 ASSOC_RESULT_AUTH_REFUSED
	 *    others                  ->      0x0003 ASSOC_RESULT_REFUSED
	 *
	 * Other response codes:
	 *    0x0001 -> ASSOC_RESULT_INVALID_PARAMETERS (unused)
	 *    0x0002 -> ASSOC_RESULT_TIMEOUT (internal timer expired waiting for
	 *                                    association response from the AP)
	 */

	status_code = le16_to_cpu(passocrsp->statuscode);
	switch (status_code) {
	case 0x00:
		break;
	case 0x01:
		lbs_deb_assoc("ASSOC_RESP: invalid parameters\n");
		//printk("ASSOC_RESP: invalid parameters\n");
		break;
	case 0x02:
		printk("ASSOC_RESP: internal timer expired while waiting for the AP\n");
		break;
	case 0x03:
		lbs_deb_assoc("ASSOC_RESP: association refused by AP\n");
		break;
	case 0x04:
		lbs_deb_assoc("ASSOC_RESP: authentication refused by AP\n");
		break;
	default:
		lbs_deb_assoc("ASSOC_RESP: failure reason 0x%02x unknown\n", status_code);
		break;
	}

	if (status_code) 
	{
		struct assoc_request * assoc_req = NULL;

		//printk("Key mgmt: %d\n", priv->in_progress_assoc_req->secinfo.key_mgmt);
		//printk("auth mode: %d\n", priv->in_progress_assoc_req->secinfo.auth_mode);

//#if (NEW_MV8686_PS == 1)
#if 0
		if ((status_code == 0x02) && (priv->associate_retry < 10))
		{
			priv->associate_retry++;
			ret = RET_ASSOC_RETRY;
			goto done;
		}
		else
			priv->associate_retry = 0;
#endif
		if ((wep_assoc_retry == 0) &&
		   (priv->in_progress_assoc_req->secinfo.key_mgmt == 0) &&
		   (priv->in_progress_assoc_req->secinfo.auth_mode == 2))
		{
			//printk("Need to restart associate for WEP.\n");
			//mutex_lock(&priv->lock);
			wep_assoc_retry = 1;
			priv->in_progress_assoc_req->secinfo.auth_mode = 1;
			assoc_req = priv->in_progress_assoc_req;
			priv->pending_assoc_req = assoc_req;
			priv->in_progress_assoc_req = NULL;
			//mutex_unlock(&priv->lock);
			//lbs_postpone_association_work2(priv);
		}
		else
		{
			printk("%s: association fail.status_code=%d\n", __func__, status_code);
			lbs_mac_event_disconnected(priv);
			ret = -1;
		}
		goto done;
	}

	wep_assoc_retry = 0;

#if (NEW_MV8686_PS == 1)
	priv->associate_retry = 0;
#endif

	lbs_deb_hex(LBS_DEB_ASSOC, "ASSOC_RESP", (void *)&resp->params,
		le16_to_cpu(resp->size) - S_DS_GEN);

	/*
	 * Send a Media Connected event, according to the Spec.
	 */
	printk("%s: association successfully.\n", __func__);
	priv->connect_status = LBS_CONNECTED;

#if (NEW_MV8686_PS == 1)
	/*
	 * Add a timer to check previous association status.
	 */
	priv->need_reassoc = 0;
	mod_timer(&priv->disconnect_timer, jiffies + msecs_to_jiffies(8 * 1000));
#endif

	/* Update current SSID and BSSID */
	memcpy(&priv->curbssparams.ssid, &bss->ssid, IW_ESSID_MAX_SIZE);
	priv->curbssparams.ssid_len = bss->ssid_len;
	memcpy(priv->curbssparams.bssid, bss->bssid, ETH_ALEN);

	priv->SNR[TYPE_RXPD][TYPE_AVG] = 0;
	priv->NF[TYPE_RXPD][TYPE_AVG] = 0;

	memset(priv->rawSNR, 0x00, sizeof(priv->rawSNR));
	memset(priv->rawNF, 0x00, sizeof(priv->rawNF));
	priv->nextSNRNF = 0;
	priv->numSNRNF = 0;

	netif_carrier_on(priv->dev);
	if (!priv->tx_pending_len)
		netif_wake_queue(priv->dev);

	memcpy(wrqu.ap_addr.sa_data, priv->curbssparams.bssid, ETH_ALEN);
	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	wireless_send_event(priv->dev, SIOCGIWAP, &wrqu, NULL);

done:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}

#if (MV8686_SUPPORT_ADHOC == 1)
static int lbs_adhoc_post(struct lbs_private *priv, struct cmd_header *resp)
{
	int ret = 0;
	u16 command = le16_to_cpu(resp->command);
	u16 result = le16_to_cpu(resp->result);
	struct cmd_ds_802_11_ad_hoc_result *adhoc_resp;
	union iwreq_data wrqu;
	struct bss_descriptor *bss;
	DECLARE_SSID_BUF(ssid);

	lbs_deb_enter(LBS_DEB_JOIN);

	adhoc_resp = (struct cmd_ds_802_11_ad_hoc_result *) resp;

	if (!priv->in_progress_assoc_req) {
		lbs_deb_join("ADHOC_RESP: no in-progress association "
			"request\n");
		ret = -1;
		goto done;
	}
	bss = &priv->in_progress_assoc_req->bss;

	/*
	 * Join result code 0 --> SUCCESS
	 */
	if (result) {
		lbs_deb_join("ADHOC_RESP: failed (result 0x%X)\n", result);
		if (priv->connect_status == LBS_CONNECTED)
			lbs_mac_event_disconnected(priv);
		ret = -1;
		goto done;
	}

	/* Send a Media Connected event, according to the Spec */
	priv->connect_status = LBS_CONNECTED;

	if (command == CMD_RET(CMD_802_11_AD_HOC_START)) {
		/* Update the created network descriptor with the new BSSID */
		memcpy(bss->bssid, adhoc_resp->bssid, ETH_ALEN);
	}

	/* Set the BSSID from the joined/started descriptor */
	memcpy(&priv->curbssparams.bssid, bss->bssid, ETH_ALEN);

	/* Set the new SSID to current SSID */
	memcpy(&priv->curbssparams.ssid, &bss->ssid, IW_ESSID_MAX_SIZE);
	priv->curbssparams.ssid_len = bss->ssid_len;

	netif_carrier_on(priv->dev);
	if (!priv->tx_pending_len)
		netif_wake_queue(priv->dev);

	memset(&wrqu, 0, sizeof(wrqu));
	memcpy(wrqu.ap_addr.sa_data, priv->curbssparams.bssid, ETH_ALEN);
	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	wireless_send_event(priv->dev, SIOCGIWAP, &wrqu, NULL);

	lbs_deb_join("ADHOC_RESP: Joined/started '%s', BSSID %pM, channel %d\n",
		     mv8686_print_ssid(ssid, bss->ssid, bss->ssid_len),
		     priv->curbssparams.bssid,
		     priv->curbssparams.channel);

done:
	lbs_deb_leave_args(LBS_DEB_JOIN, "ret %d", ret);
	return ret;
}
#endif

