/*
* Copyright (c) 2013-2016 Cisco Systems, Inc. and others.  All rights reserved.
*
* This program and the accompanying materials are made available under the
* terms of the Eclipse Public License v1.0 which accompanies this distribution,
* and is available at http://www.eclipse.org/legal/epl-v10.html
*
*/

#include "parseBgpLibMpReach.h"
#include "parseBgpLib.h"
#include "Logger.h"

#include <arpa/inet.h>

namespace parse_bgp_lib {

/**
* Constructor for class
*
* \details Handles BGP MP Reach NLRI
*
* \param [in]     logPtr                   Pointer to existing Logger for app logging
* \param [in]     enable_debug             Debug true to enable, false to disable
*/
MPReachAttr::MPReachAttr(parseBgpLib *parse_lib, Logger *logPtr, bool enable_debug)
        : logger{logPtr}, debug{enable_debug}, caller{parse_lib}{
}

MPReachAttr::~MPReachAttr() {
}

/**
* Parse the MP_REACH NLRI attribute data
*
* \details
*      Will parse the MP_REACH_NLRI data passed.  Parsed data will be stored
*      in parsed_data.
*
*      \see RFC4760 for format details.
*
* \param [in]   attr_len               Length of the attribute data
* \param [in]   data                   Pointer to the attribute data
* \param [out]  parsed_update  Reference to parsed_update; will be updated with all parsed data
*/
void MPReachAttr::parseReachNlriAttr(int attr_len, u_char *data, parse_bgp_lib::parseBgpLib::parsed_update &update) {
    mp_reach_nlri nlri;
    /*
     * Set the MP NLRI struct
     */
    // Read address family
    memcpy(&nlri.afi, data, 2); data += 2; attr_len -= 2;
    parse_bgp_lib::SWAP_BYTES(&nlri.afi);                     // change to host order

    nlri.safi = *data++; attr_len--;                 // Set the SAFI - 1 octet
    nlri.nh_len = *data++; attr_len--;              // Set the next-hop length - 1 octet
    nlri.next_hop = data;  data += nlri.nh_len; attr_len -= nlri.nh_len;    // Set pointer position for nh data
    nlri.reserved = *data++; attr_len--;             // Set the reserve octet
    nlri.nlri_data = data;                          // Set pointer position for nlri data
    nlri.nlri_len = attr_len;                       // Remaining attribute length is for NLRI data

    /*
     * Make sure the parsing doesn't exceed buffer
     */
    if (attr_len < 0) {
        LOG_NOTICE("MP_REACH NLRI data length is larger than attribute data length, skipping parse");
        return;
    }

    SELF_DEBUG("afi=%d safi=%d nh_len=%d reserved=%d",
               nlri.afi, nlri.safi, nlri.nh_len, nlri.reserved);

    /*
     * Next-hop and NLRI data depends on the AFI & SAFI
     *  Parse data based on AFI + SAFI
     */
    parseAfi(nlri, update);
}


/**
* MP Reach NLRI parse based on AFI
*
* \details Will parse the next-hop and nlri data based on AFI.  A call to
*          the specific SAFI method will be performed to further parse the message.
*
* \param [in]   nlri           Reference to parsed NLRI struct
* \param [out]  parsed_update  Reference to parsed_update; will be updated with all parsed data
*/
void MPReachAttr::parseAfi(mp_reach_nlri &nlri, parse_bgp_lib::parseBgpLib::parsed_update &update) {

    switch (nlri.afi) {
        case parse_bgp_lib::BGP_AFI_IPV6 :  // IPv6
            parseAfi_IPv4IPv6(false, nlri, update);
            break;

        case parse_bgp_lib::BGP_AFI_IPV4 : // IPv4
            parseAfi_IPv4IPv6(true, nlri, update);
            break;

        case parse_bgp_lib::BGP_AFI_BGPLS : // BGP-LS (draft-ietf-idr-ls-distribution-10)
        {
//                MPLinkState ls(logger, peer_addr, &parsed_data, debug);
//                ls.parseReachLinkState(nlri);

            break;
        }

        default : // Unknown
            LOG_INFO("MP_REACH AFI=%d is not implemented yet, skipping", nlri.afi);
            return;
    }
}

/**
* MP Reach NLRI parse for BGP_AFI_IPv4 & BGP_AFI_IPV6
*
* \details Will handle parsing the SAFI's for address family ipv6 and IPv4
*
* \param [in]   isIPv4         True false to indicate if IPv4 or IPv6
* \param [in]   nlri           Reference to parsed NLRI struct
* \param [out]  parsed_update  Reference to parsed_update; will be updated with all parsed data
*/
void MPReachAttr::parseAfi_IPv4IPv6(bool isIPv4, mp_reach_nlri &nlri, parse_bgp_lib::parseBgpLib::parsed_update &update) {
    u_char      ip_raw[16];
    char        ip_char[40];

    bzero(ip_raw, sizeof(ip_raw));

    /*
     * Decode based on SAFI
     */
    switch (nlri.safi) {
        case parse_bgp_lib::BGP_SAFI_UNICAST: // Unicast IP address prefix

            // Next-hop is an IP address - Change/set the next-hop attribute in parsed data to use this next-hop
            if (nlri.nh_len > 16)
                memcpy(ip_raw, nlri.next_hop, 16);
            else
                memcpy(ip_raw, nlri.next_hop, nlri.nh_len);

            if (not isIPv4)
                inet_ntop(AF_INET6, ip_raw, ip_char, sizeof(ip_char));
            else
                inet_ntop(AF_INET, ip_raw, ip_char, sizeof(ip_char));
            update.attrs[LIB_ATTR_NEXT_HOP].official_type = ATTR_TYPE_NEXT_HOP;
            update.attrs[LIB_ATTR_NEXT_HOP].attr_name.assign("nextHop");
            update.attrs[LIB_ATTR_NEXT_HOP].attr_value.push_back(std::string(ip_char));

            // Data is an IP address - parse the address and save it
            parseNlriData_IPv4IPv6(isIPv4, nlri.nlri_data, nlri.nlri_len, update.nlri_list, caller, debug, logger);
            break;

        case parse_bgp_lib::BGP_SAFI_NLRI_LABEL:
            // Next-hop is an IP address - Change/set the next-hop attribute in parsed data to use this next-hop
            if (nlri.nh_len > 16)
                memcpy(ip_raw, nlri.next_hop, 16);
            else
                memcpy(ip_raw, nlri.next_hop, nlri.nh_len);

            if (not isIPv4)
                inet_ntop(AF_INET6, ip_raw, ip_char, sizeof(ip_char));
            else
                inet_ntop(AF_INET, ip_raw, ip_char, sizeof(ip_char));

            update.attrs[LIB_ATTR_NEXT_HOP].official_type = ATTR_TYPE_NEXT_HOP;
            update.attrs[LIB_ATTR_NEXT_HOP].attr_name.assign("nextHop");
            update.attrs[LIB_ATTR_NEXT_HOP].attr_value.push_back(std::string(ip_char));

            // Data is an Label, IP address tuple parse and save it
            parseNlriData_LabelIPv4IPv6(isIPv4, nlri.nlri_data, nlri.nlri_len, update.nlri_list, caller, debug, logger);
            break;

        default :
            LOG_INFO("MP_REACH AFI=ipv4/ipv6 (%d) SAFI=%d is not implemented yet, skipping for now",
                     isIPv4, nlri.safi);
            return;
    }
}

/**
* Parses mp_reach_nlri and mp_unreach_nlri (IPv4/IPv6)
*
* \details
*      Will parse the NLRI encoding as defined in RFC4760 Section 5 (NLRI Encoding).
*
* \param [in]   isIPv4                 True false to indicate if IPv4 or IPv6
* \param [in]   data                   Pointer to the start of the prefixes to be parsed
* \param [in]   len                    Length of the data in bytes to be read
* \param [out]  nlri_list              Reference to a list<parse_bgp_lib_nlri> to be updated with entries
*/
void MPReachAttr::parseNlriData_IPv4IPv6(bool isIPv4, u_char *data, uint16_t len,
                                         std::list<parseBgpLib::parse_bgp_lib_nlri> &nlri_list,
                                         parseBgpLib *parser, bool debug, Logger *logger) {
    u_char            ip_raw[16];
    char              ip_char[40];
    u_char            addr_bytes;
    uint32_t          path_id;
    u_char            prefix_len;
    std::ostringstream numString;

    parseBgpLib::parse_bgp_lib_nlri nlri;

    if (len <= 0 or data == NULL)
        return;

    // TODO: Can extend this to support multicast, but right now we set it to unicast v4/v6
    nlri.afi = isIPv4 ? parse_bgp_lib::BGP_AFI_IPV4 : parse_bgp_lib::BGP_AFI_IPV6;
    nlri.safi = parse_bgp_lib::BGP_SAFI_UNICAST;
    nlri.type = parse_bgp_lib::NLRI_TYPE_NONE;


    // Loop through all prefixes
    for (size_t read_size=0; read_size < len; read_size++) {
        bzero(ip_raw, sizeof(ip_raw));

        // Parse add-paths if enabled
        if (parser->getAddpathCapability(nlri.afi, nlri.safi)
            and (len - read_size) >= 4) {
            memcpy(&path_id, data, 4);
            parse_bgp_lib::SWAP_BYTES(&path_id);
            data += 4;
            read_size += 4;
        } else
            path_id = 0;

        numString.str(std::string());
        numString << path_id;
        nlri.nlri[LIB_NLRI_PATH_ID].push_back(numString.str());

        // set the address in bits length
        prefix_len = *data++;
        numString.str(std::string());
        numString << static_cast<unsigned>(prefix_len);
        nlri.nlri[LIB_NLRI_PREFIX_LENGTH].push_back(numString.str());

        // Figure out how many bytes the bits requires
        addr_bytes = prefix_len / 8;
        if (prefix_len % 8)
            ++addr_bytes;

        SELF_DEBUG("Reading NLRI data prefix bits=%d bytes=%d", prefix_len, addr_bytes);

        memcpy(ip_raw, data, addr_bytes);
        data += addr_bytes;
        read_size += addr_bytes;

        // Convert the IP to string printed format
        if (isIPv4)
            inet_ntop(AF_INET, ip_raw, ip_char, sizeof(ip_char));
        else
            inet_ntop(AF_INET6, ip_raw, ip_char, sizeof(ip_char));

        nlri.nlri[LIB_NLRI_PREFIX].push_back(ip_char);
        SELF_DEBUG("Adding prefix %s len %d", ip_char, prefix_len);

        // set the raw/binary address
        nlri.nlri[LIB_NLRI_PREFIX_BIN].push_back(std::string(ip_raw, ip_raw + 4));

        // Add tuple to prefix list
        nlri_list.push_back(nlri);
    }
}

/**
* Parses mp_reach_nlri and mp_unreach_nlri (IPv4/IPv6)
*
* \details
*      Will parse the NLRI encoding as defined in RFC3107 Section 3 (Carrying Label Mapping information).
*
* \param [in]   isIPv4                 True false to indicate if IPv4 or IPv6
* \param [in]   data                   Pointer to the start of the label + prefixes to be parsed
* \param [in]   len                    Length of the data in bytes to be read
* \param [out]  nlri_list              Reference to a list<parse_bgp_lib_nlri> to be updated with entries
*/
void MPReachAttr::parseNlriData_LabelIPv4IPv6(bool isIPv4, u_char *data, uint16_t len,
                                              std::list<parseBgpLib::parse_bgp_lib_nlri> &nlri_list,
                                              parseBgpLib *parser, bool debug, Logger *logger) {
    u_char ip_raw[16];
    char ip_char[40];
    int addr_bytes;
    uint32_t path_id;
    u_char prefix_len;
    std::ostringstream numString;

    parseBgpLib::parse_bgp_lib_nlri nlri;

    typedef union {
        struct {
            uint8_t ttl     : 8;          // TTL - not present since only 3 octets are used
            uint8_t bos     : 1;          // Bottom of stack
            uint8_t exp     : 3;          // EXP - not really used
            uint32_t value   : 20;         // Label value
        } decode;
        uint32_t data;                 // Raw label - 3 octets only per RFC3107
    } mpls_label;

    mpls_label label;

    if (len <= 0 or data == NULL)
        return;

    nlri.afi = isIPv4 ? parse_bgp_lib::BGP_AFI_IPV4 : parse_bgp_lib::BGP_AFI_IPV6;
    nlri.safi = parse_bgp_lib::BGP_SAFI_NLRI_LABEL;
    nlri.type = parse_bgp_lib::NLRI_TYPE_NONE;

    int parsed_bytes = 0;

    // Loop through all prefixes
    for (size_t read_size = 0; read_size < len; read_size++) {

        // Parse add-paths if enabled
        if (parser->getAddpathCapability(nlri.afi, nlri.safi)
            and (len - read_size) >= 4) {
            memcpy(&path_id, data, 4);
            parse_bgp_lib::SWAP_BYTES(&path_id);
            data += 4;
            read_size += 4;
        } else
            path_id = 0;

        numString.str(std::string());
        numString << path_id;
        nlri.nlri[LIB_NLRI_PATH_ID].push_back(numString.str());

        if (parsed_bytes == len) {
            break;
        }

        bzero(&label, sizeof(label));
        bzero(ip_raw, sizeof(ip_raw));

        // set the address in bits length
        prefix_len = *data++;

        // Figure out how many bytes the bits requires
        addr_bytes = prefix_len / 8;
        if (prefix_len % 8)
            ++addr_bytes;

        SELF_DEBUG("Reading NLRI data prefix bits=%d bytes=%d", prefix_len, addr_bytes);

        // the label is 3 octets long
        while (addr_bytes >= 3) {
            memcpy(&label.data, data, 3);
            parse_bgp_lib::SWAP_BYTES(&label.data);     // change to host order

            data += 3;
            addr_bytes -= 3;
            read_size += 3;
            prefix_len -= 24;        // Update prefix len to not include the label just parsed
            numString.str(std::string());
            numString << static_cast<unsigned>(prefix_len);
            nlri.nlri[LIB_NLRI_PREFIX_LENGTH].push_back(numString.str());

            std::ostringstream convert;
            convert << label.decode.value;
            nlri.nlri[LIB_NLRI_LABELS].push_back(convert.str());

            if (label.decode.bos == 1 or label.data == 0x80000000 /* withdrawn label as 32bits instead of 24 */) {
                break;               // Reached EoS

            }
        }

        memcpy(ip_raw, data, addr_bytes);
        data += addr_bytes;
        read_size += addr_bytes;

        // Convert the IP to string printed format
        if (isIPv4)
            inet_ntop(AF_INET, ip_raw, ip_char, sizeof(ip_char));
        else
            inet_ntop(AF_INET6, ip_raw, ip_char, sizeof(ip_char));

        nlri.nlri[LIB_NLRI_PREFIX].push_back(ip_char);
        SELF_DEBUG("Adding prefix %s len %d", ip_char, prefix_len);

        // set the raw/binary address
        nlri.nlri[LIB_NLRI_PREFIX_BIN].push_back(std::string(ip_raw, ip_raw + 4));

        // Add tuple to prefix list
        nlri_list.push_back(nlri);
    }
}

} /* namespace parse_bgp_lib */
