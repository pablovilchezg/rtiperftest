/*
 * (c) 2005-2017  Copyright, Real-Time Innovations, Inc. All rights reserved.
 * Subject to Eclipse Public License v1.0; see LICENSE.md for details.
 */

#include "PerftestTransport.h"
#include "ParameterManager.h"

/******************************************************************************/

#if defined(RTI_WIN32)
  #pragma warning(push)
  #pragma warning(disable : 4996)
  #define STRNCASECMP _strnicmp
#elif defined(RTI_VXWORKS)
  #define STRNCASECMP strncmp
#else
  #define STRNCASECMP strncasecmp
#endif
#define IS_OPTION(str, option) (STRNCASECMP(str, option, strlen(str)) == 0)

/******************************************************************************/

// Tag used when adding logging output.
const std::string classLoggingString = "PerftestTransport:";

std::map<std::string, TransportConfig> PerftestTransport::transportConfigMap;

/******************************************************************************/
/* DDS Related functions. These functions doesn't belong to the
 * PerftestTransport class. This way we decouple the Transport class from
 * the specific implementation (C++ Classic vs C++PSM).
 */

bool addPropertyToParticipantQos(
        DDS_DomainParticipantQos &qos,
        std::string propertyName,
        std::string propertyValue)
{

    DDS_ReturnCode_t retcode = DDSPropertyQosPolicyHelper::add_property(
            qos.property,
            propertyName.c_str(),
            propertyValue.c_str(),
            false);
    if (retcode != DDS_RETCODE_OK) {
        fprintf(
                stderr,
                "%s Failed to add property \"%s\"\n",
                classLoggingString.c_str(),
                propertyName.c_str());
        return false;
    }

    return true;
}

bool assertPropertyToParticipantQos(
        DDS_DomainParticipantQos &qos,
        std::string propertyName,
        std::string propertyValue)
{
    DDS_ReturnCode_t retcode = DDSPropertyQosPolicyHelper::assert_property(
            qos.property,
            propertyName.c_str(),
            propertyValue.c_str(),
            false);
    if (retcode != DDS_RETCODE_OK) {
        fprintf(
                stderr,
                "%s Failed to add property \"%s\"\n",
                classLoggingString.c_str(),
                propertyName.c_str());
        return false;
    }

    return true;
}

bool setAllowInterfacesList(
        PerftestTransport &transport,
        DDS_DomainParticipantQos &qos)
{
    if (!PM::GetInstance().get<std::string>("allowInterfaces").empty()) {

        if (transport.transportConfig.kind == TRANSPORT_NOT_SET) {
            fprintf(stderr,
                    "%s Ignoring -nic/-allowInterfaces option\n",
                    classLoggingString.c_str());
            return true;
        }

        /*
         * In these 2 specific cases, we are forced to add 2 properties, one
         * for UDPv4 and another for UDPv6
         */
        if (transport.transportConfig.kind == TRANSPORT_UDPv4_UDPv6_SHMEM
                || transport.transportConfig.kind == TRANSPORT_UDPv4_UDPv6) {

            std::string propertyName =
                    "dds.transport.UDPv4.builtin.parent.allow_interfaces";

            if (!addPropertyToParticipantQos(
                    qos,
                    propertyName,
                    PM::GetInstance().get<std::string>("allowInterfaces"))) {
                return false;
            }

            propertyName =
                    "dds.transport.UDPv6.builtin.parent.allow_interfaces";

            if (!addPropertyToParticipantQos(
                    qos,
                    propertyName,
                    PM::GetInstance().get<std::string>("allowInterfaces"))) {
                return false;
            }

        } else {

            std::string propertyName = transport.transportConfig.prefixString;
            if (transport.transportConfig.kind == TRANSPORT_WANv4) {
                propertyName += ".parent";
            }

            propertyName += ".parent.allow_interfaces";

            return addPropertyToParticipantQos(
                    qos,
                    propertyName,
                    PM::GetInstance().get<std::string>("allowInterfaces"));
        }
    }

    return true;
}

bool setTransportVerbosity(
        PerftestTransport &transport,
        DDS_DomainParticipantQos &qos)
{
    if (!PM::GetInstance().get<std::string>("transportVerbosity").empty()) {
        if (transport.transportConfig.kind == TRANSPORT_NOT_SET) {
            fprintf(stderr,
                    "%s Ignoring -transportVerbosity option\n",
                    classLoggingString.c_str());
            return true;
        }

        std::string propertyName = transport.transportConfig.prefixString
                + ".verbosity";

        // The name of the property in TCPv4 is different
        if (transport.transportConfig.kind == TRANSPORT_TCPv4) {
            propertyName = transport.transportConfig.prefixString
                    + ".logging_verbosity_bitmap";
        } else if (transport.transportConfig.kind == TRANSPORT_UDPv4
                || transport.transportConfig.kind == TRANSPORT_UDPv6
                || transport.transportConfig.kind == TRANSPORT_SHMEM
                || transport.transportConfig.kind == TRANSPORT_UDPv4_SHMEM
                || transport.transportConfig.kind == TRANSPORT_UDPv6_SHMEM
                || transport.transportConfig.kind == TRANSPORT_UDPv4_UDPv6
                || transport.transportConfig.kind == TRANSPORT_UDPv4_UDPv6_SHMEM) {
            // We do not change logging for the builtin transports.
            return true;
        }

        return addPropertyToParticipantQos(
                qos,
                propertyName,
                PM::GetInstance().get<std::string>("transportVerbosity"));
    }
    return true;
}

bool configureSecurityFiles(
        PerftestTransport &transport,
        DDS_DomainParticipantQos& qos)
{

    if (!PM::GetInstance().get<std::string>("transportCertAuthority").empty()) {
        if (!addPropertyToParticipantQos(
                qos,
                transport.transportConfig.prefixString + ".tls.verify.ca_file",
                PM::GetInstance().get<std::string>("transportCertAuthority"))) {
            return false;
        }
    }

    if (!PM::GetInstance().get<std::string>("transportCertFile").empty()) {
        if (!addPropertyToParticipantQos(
                qos,
                transport.transportConfig.prefixString
                        + ".tls.identity.certificate_chain_file",
                PM::GetInstance().get<std::string>("transportCertFile"))) {
            return false;
        }
    }

    if (!PM::GetInstance().get<std::string>("transportPrivateKey").empty()) {
        if (!addPropertyToParticipantQos(
                qos,
                transport.transportConfig.prefixString
                        + ".tls.identity.private_key_file",
                PM::GetInstance().get<std::string>("transportPrivateKey"))) {
            return false;
        }
    }

    return true;
}

bool configureTcpTransport(
        PerftestTransport &transport,
        DDS_DomainParticipantQos& qos)
{
    std::string serverBindPort = PM::GetInstance().get<std::string>("transportServerBindPort");
    qos.transport_builtin.mask = DDS_TRANSPORTBUILTIN_MASK_NONE;

    if (!addPropertyToParticipantQos(
            qos,
            std::string("dds.transport.load_plugins"),
            transport.transportConfig.prefixString)) {
        return false;
    }

    if (!serverBindPort.empty()) {
        if (!addPropertyToParticipantQos(
                qos,
                transport.transportConfig.prefixString + ".server_bind_port",
                serverBindPort)) {
            return false;
        }
    }

    if (PM::GetInstance().get<bool>("transportWan")) {

        if (!assertPropertyToParticipantQos(
                qos,
                transport.transportConfig.prefixString
                        + ".parent.classid",
                "NDDS_TRANSPORT_CLASSID_TCPV4_WAN")) {
            return false;
        }

        if (serverBindPort != "0") {
            if (!PM::GetInstance()
                         .get<std::string>("transportPublicAddress")
                         .empty()) {
                if (!addPropertyToParticipantQos(
                        qos,
                        transport.transportConfig.prefixString
                                + ".public_address",
                        PM::GetInstance().get<std::string>(
                                "transportPublicAddress"))) {
                    return false;
                }
            } else {
                fprintf(stderr,
                        "%s Public Address is required if "
                        "Server Bind Port != 0\n",
                        classLoggingString.c_str());
                return false;
            }
        }
    }

    if (transport.transportConfig.kind == TRANSPORT_TLSv4) {
        if (PM::GetInstance().get<bool>("transportWan")) {
            if (!assertPropertyToParticipantQos(
                    qos,
                    transport.transportConfig.prefixString
                            + ".parent.classid",
                    "NDDS_TRANSPORT_CLASSID_TLSV4_WAN")) {
                return false;
            }
        } else {
            if (!assertPropertyToParticipantQos(
                    qos,
                    transport.transportConfig.prefixString
                            + ".parent.classid",
                    "NDDS_TRANSPORT_CLASSID_TLSV4_LAN")) {
                return false;
            }
        }

        if (!configureSecurityFiles(transport, qos)) {
            return false;
        }
    }

    return true;
}

bool configureDtlsTransport(
        PerftestTransport &transport,
        DDS_DomainParticipantQos& qos)
{

    qos.transport_builtin.mask = DDS_TRANSPORTBUILTIN_MASK_NONE;

    if (!addPropertyToParticipantQos(
            qos,
            std::string("dds.transport.load_plugins"),
            transport.transportConfig.prefixString)) {
        return false;
    }

    if (!addPropertyToParticipantQos(
            qos,
            transport.transportConfig.prefixString + ".library",
            "nddstransporttls")) {
        return false;
    }

    if (!addPropertyToParticipantQos(
            qos,
            transport.transportConfig.prefixString + ".create_function",
            "NDDS_Transport_DTLS_create")) {
        return false;
    }

    if (!configureSecurityFiles(transport, qos)) {
        return false;
    }

    return true;
}

bool configureWanTransport(
        PerftestTransport &transport,
        DDS_DomainParticipantQos& qos)
{

    qos.transport_builtin.mask = DDS_TRANSPORTBUILTIN_MASK_NONE;

    if (!addPropertyToParticipantQos(
            qos,
            std::string("dds.transport.load_plugins"),
            transport.transportConfig.prefixString)) {
        return false;
    }

    if (!addPropertyToParticipantQos(
            qos,
            transport.transportConfig.prefixString + ".library",
            "nddstransportwan")) {
        return false;
    }

    if (!addPropertyToParticipantQos(
            qos,
            transport.transportConfig.prefixString + ".create_function",
            "NDDS_Transport_WAN_create")) {
        return false;
    }

    if (!PM::GetInstance()
            .get<std::string>("transportWanServerAddress")
            .empty()) {
        if (!addPropertyToParticipantQos(
                qos,
                transport.transportConfig.prefixString + ".server",
                PM::GetInstance().get<std::string>(
                        "transportWanServerAddress"))) {
            return false;
        }
    } else {
        fprintf(
                stderr,
                "%s Wan Server Address is required\n",
                classLoggingString.c_str());
        return false;
    }

    if (!PM::GetInstance().get<std::string>("transportWanServerPort").empty()) {
        if (!addPropertyToParticipantQos(
                qos,
                transport.transportConfig.prefixString + ".server_port",
                PM::GetInstance().get<std::string>("transportWanServerPort"))) {
            return false;
        }
    }

    if (!PM::GetInstance().get<std::string>("transportWanId").empty()) {
        if (!addPropertyToParticipantQos(
                qos,
                transport.transportConfig.prefixString
                        + ".transport_instance_id",
                PM::GetInstance().get<std::string>("transportWanId"))) {
            return false;
        }
    } else {
        fprintf(stderr, "%s Wan ID is required\n", classLoggingString.c_str());
        return false;
    }

    if (PM::GetInstance().get<bool>("transportSecureWan")) {
        if (!addPropertyToParticipantQos(
                qos,
                transport.transportConfig.prefixString + ".enable_security",
                "1")) {
            return false;
        }

        if (!configureSecurityFiles(transport, qos)) {
            return false;
        }
    }

    return true;
}

bool configureShmemTransport(
        PerftestTransport &transport,
        DDS_DomainParticipantQos& qos)
{
    // Number of messages that can be buffered in the receive queue.
    int received_message_count_max = 1024 * 1024 * 2
            / (int) PM::GetInstance().get<unsigned long long>("dataLen");
    if (received_message_count_max < 1) {
        received_message_count_max = 1;
    }

    std::ostringstream string_stream_object;
    string_stream_object << received_message_count_max;
    if (!assertPropertyToParticipantQos(
            qos,
            "dds.transport.shmem.builtin.received_message_count_max",
            string_stream_object.str())) {
        return false;
    }
    return true;
}

bool configureTransport(
        PerftestTransport &transport,
        DDS_DomainParticipantQos &qos)
{

    /*
     * If transportConfig.kind is not set, then we want to use the value
     * provided by the Participant Qos, so first we read it from there and
     * update the value of transportConfig.kind with whatever was already set.
     */

    if (transport.transportConfig.kind == TRANSPORT_NOT_SET) {

        DDS_TransportBuiltinKindMask mask = qos.transport_builtin.mask;
        switch (mask) {
            case DDS_TRANSPORTBUILTIN_UDPv4:
                transport.transportConfig = TransportConfig(
                    TRANSPORT_UDPv4,
                    "UDPv4",
                    "dds.transport.UDPv4.builtin");
                break;
            case DDS_TRANSPORTBUILTIN_UDPv6:
                transport.transportConfig = TransportConfig(
                    TRANSPORT_UDPv6,
                    "UDPv6",
                    "dds.transport.UDPv6.builtin");
                break;
            case DDS_TRANSPORTBUILTIN_SHMEM:
                transport.transportConfig = TransportConfig(
                    TRANSPORT_SHMEM,
                    "SHMEM",
                    "dds.transport.shmem.builtin");
                break;
            case DDS_TRANSPORTBUILTIN_UDPv4|DDS_TRANSPORTBUILTIN_SHMEM:
                transport.transportConfig = TransportConfig(
                    TRANSPORT_UDPv4_SHMEM,
                    "UDPv4 & SHMEM",
                    "dds.transport.UDPv4.builtin");
                break;
            case DDS_TRANSPORTBUILTIN_UDPv4|DDS_TRANSPORTBUILTIN_UDPv6:
                transport.transportConfig = TransportConfig(
                    TRANSPORT_UDPv4_UDPv6,
                    "UDPv4 & UDPv6",
                    "dds.transport.UDPv4.builtin");
                break;
            case DDS_TRANSPORTBUILTIN_SHMEM|DDS_TRANSPORTBUILTIN_UDPv6:
                transport.transportConfig = TransportConfig(
                    TRANSPORT_UDPv6_SHMEM,
                    "UDPv6 & SHMEM",
                    "dds.transport.UDPv6.builtin");
                break;
            case DDS_TRANSPORTBUILTIN_UDPv4|DDS_TRANSPORTBUILTIN_UDPv6|DDS_TRANSPORTBUILTIN_SHMEM:
                transport.transportConfig = TransportConfig(
                    TRANSPORT_UDPv4_UDPv6_SHMEM,
                    "UDPv4 & UDPv6 & SHMEM",
                    "dds.transport.UDPv4.builtin");
                break;
            default:
                /*
                 * This would mean that the mask is either empty or a
                 * different value that we do not support yet. So we keep
                 * the value as "TRANSPORT_NOT_SET"
                 */
                break;
        }
        transport.transportConfig.takenFromQoS = true;
    }

    switch (transport.transportConfig.kind) {

        case TRANSPORT_UDPv4:
            qos.transport_builtin.mask = DDS_TRANSPORTBUILTIN_UDPv4;
            break;

        case TRANSPORT_UDPv6:
            qos.transport_builtin.mask = DDS_TRANSPORTBUILTIN_UDPv6;
            break;

        case TRANSPORT_SHMEM:
            qos.transport_builtin.mask = DDS_TRANSPORTBUILTIN_SHMEM;
            if (!configureShmemTransport(transport, qos)) {
                fprintf(stderr,
                        "%s Failed to configure SHMEM plugin\n",
                        classLoggingString.c_str());
                return false;
            }
            break;

        case TRANSPORT_TCPv4:
            if (!configureTcpTransport(transport, qos)) {
                fprintf(stderr,
                        "%s Failed to configure TCP plugin\n",
                        classLoggingString.c_str());
                return false;
            }
            break;

        case TRANSPORT_TLSv4:
            if (!configureTcpTransport(transport, qos)) {
                fprintf(stderr,
                        "%s Failed to configure TCP + TLS plugin\n",
                        classLoggingString.c_str());
                return false;
            }
            break;

        case TRANSPORT_DTLSv4:
            if (!configureDtlsTransport(transport, qos)) {
                fprintf(stderr,
                        "%s Failed to configure DTLS plugin\n",
                        classLoggingString.c_str());
                return false;
            }
            break;

        case TRANSPORT_WANv4:
            if (!configureWanTransport(transport, qos)) {
                fprintf(stderr,
                        "%s Failed to configure WAN plugin\n",
                        classLoggingString.c_str());
                return false;
            }
            break;
        default:

            /*
             * If shared memory is enabled we want to set up its
             * specific configuration
             */
            if ((qos.transport_builtin.mask & DDS_TRANSPORTBUILTIN_SHMEM)
                    != 0) {
                if (!configureShmemTransport(transport, qos)) {
                    fprintf(stderr,
                            "%s Failed to configure SHMEM plugin\n",
                            classLoggingString.c_str());
                    return false;
                }
            }
            break;
    } // Switch

    /*
     * If the transport is empty or if it is shmem, it does not make sense
     * setting an interface, in those cases, if the allow interfaces is provided
     * we empty it.
     */
    if (transport.transportConfig.kind != TRANSPORT_NOT_SET
            && transport.transportConfig.kind != TRANSPORT_SHMEM) {
        if (!setAllowInterfacesList(transport, qos)) {
            return false;
        }
    } else {
        // We are not using the allow interface string, so we clear it
        PM::GetInstance().set<std::string>("allowInterfaces", std::string(""));
    }

    if (!setTransportVerbosity(transport, qos)) {
        return false;
    }

    return true;
}

/******************************************************************************/
/* CLASS CONSTRUCTOR AND DESTRUCTOR */

PerftestTransport::PerftestTransport()
{
    multicastAddrMap[LATENCY_TOPIC_NAME] = "239.255.1.2";
    multicastAddrMap[ANNOUNCEMENT_TOPIC_NAME] = "239.255.1.100";
    multicastAddrMap[THROUGHPUT_TOPIC_NAME] = "239.255.1.1";
}

PerftestTransport::~PerftestTransport()
{
}

/******************************************************************************/
/* PRIVATE METHODS */

const std::map<std::string, TransportConfig>&
PerftestTransport::getTransportConfigMap()
{

    if (transportConfigMap.empty()) {
        transportConfigMap["Use XML"] = TransportConfig(
                TRANSPORT_NOT_SET,
                "--",
                "--");
        transportConfigMap["UDPv4"] = TransportConfig(
                TRANSPORT_UDPv4,
                "UDPv4",
                "dds.transport.UDPv4.builtin");
        transportConfigMap["UDPv6"] = TransportConfig(
                TRANSPORT_UDPv6,
                "UDPv6",
                "dds.transport.UDPv6.builtin");
        transportConfigMap["TCP"] = TransportConfig(
                TRANSPORT_TCPv4,
                "TCP",
                "dds.transport.TCPv4.tcp1");
        transportConfigMap["TLS"] = TransportConfig(
                TRANSPORT_TLSv4,
                "TLS",
                "dds.transport.TCPv4.tcp1");
        transportConfigMap["DTLS"] = TransportConfig(
                TRANSPORT_DTLSv4,
                "DTLS",
                "dds.transport.DTLS.dtls1");
        transportConfigMap["WAN"] = TransportConfig(
                TRANSPORT_WANv4,
                "WAN",
                "dds.transport.WAN.wan1");
        transportConfigMap["SHMEM"] = TransportConfig(
                TRANSPORT_SHMEM,
                "SHMEM",
                "dds.transport.shmem.builtin");
    }
    return transportConfigMap;
}

bool PerftestTransport::setTransport(std::string transportString)
{

    std::map<std::string, TransportConfig> configMap = getTransportConfigMap();
    std::map<std::string, TransportConfig>::iterator it = configMap.find(
            transportString);
    if (it != configMap.end()) {
        transportConfig = it->second;
    } else {
        fprintf(
                stderr,
                "%s \"%s\" is not a valid transport. "
                        "List of supported transport:\n",
                classLoggingString.c_str(),
                transportString.c_str());
        for (std::map<std::string, TransportConfig>::iterator it = configMap
                .begin(); it != configMap.end(); ++it) {
            fprintf(stderr, "\t\"%s\"\n", it->first.c_str());
        }
        return false;
    }
    return true;
}

void PerftestTransport::populateSecurityFiles() {

    if (PM::GetInstance().get<std::string>("transportCertFile").empty()) {
        if (PM::GetInstance().get<bool>("pub")) {
            PM::GetInstance().set(
                    "transportCertFile",
                    TRANSPORT_CERTIFICATE_FILE_PUB);
        } else {
            PM::GetInstance().set(
                    "transportCertFile",
                    TRANSPORT_CERTIFICATE_FILE_SUB);
        }
    }

    if (PM::GetInstance().get<std::string>("transportPrivateKey").empty()) {
        if (PM::GetInstance().get<bool>("pub")) {
            PM::GetInstance().set(
                    "transportPrivateKey",
                    TRANSPORT_PRIVATEKEY_FILE_PUB);
        } else {
            PM::GetInstance().set(
                    "transportPrivateKey",
                    TRANSPORT_PRIVATEKEY_FILE_SUB);
        }
    }

    if (PM::GetInstance().get<std::string>("transportCertAuthority").empty()) {
        PM::GetInstance().set(
                "transportCertAuthority",
                TRANSPORT_CERTAUTHORITY_FILE);
    }
}

/******************************************************************************/
/* PUBLIC METHODS */

std::string PerftestTransport::printTransportConfigurationSummary()
{
    bool useMulticast = PM::GetInstance().get<bool>("multicast");
    std::ostringstream stringStream;
    stringStream << "Transport Configuration:\n";
    stringStream << "\tKind: " << transportConfig.nameString;
    if (transportConfig.takenFromQoS) {
        stringStream << " (taken from QoS XML file)";
    }
    stringStream << "\n";

    if (!PM::GetInstance().get<std::string>("allowInterfaces").empty()) {
        stringStream << "\tNic: "
                     << PM::GetInstance().get<std::string>("allowInterfaces")
                     << "\n";
    }

    stringStream << "\tUse Multicast: "
                 << ((allowsMulticast() && useMulticast) ? "True" : "False");
    if (!allowsMulticast() && useMulticast) {
        stringStream << " (Multicast is not supported for "
                     << transportConfig.nameString << ")";
    }
    stringStream << "\n";

    if (transportConfig.kind == TRANSPORT_TCPv4
            || transportConfig.kind == TRANSPORT_TLSv4) {
        stringStream << "\tTCP Server Bind Port: "
                     << PM::GetInstance().get<std::string>(
                                "transportServerBindPort")
                     << "\n";

        stringStream << "\tTCP LAN/WAN mode: "
                     << (PM::GetInstance().get<bool>("transportWan")
                            ? "WAN\n" : "LAN\n");
        if (PM::GetInstance().get<bool>("transportWan")) {
            stringStream << "\tTCP Public Address: "
                         << PM::GetInstance().get<std::string>(
                                "transportPublicAddress") << "\n";
        }
    }

    if (transportConfig.kind == TRANSPORT_WANv4) {

        stringStream << "\tWAN Server Address: "
                     << PM::GetInstance().get<std::string>(
                            "transportWanServerAddress")
                     << ":"
                     << PM::GetInstance().get<std::string>(
                            "transportWanServerPort")
                     << "\n";
        stringStream << "\tWAN Id: "
                     << PM::GetInstance().get<std::string>("transportWanId")
                     << "\n";
        stringStream << "\tWAN Secure: "
                     << PM::GetInstance().get<bool>("transportSecureWan")
                     << "\n";
    }

    if (transportConfig.kind == TRANSPORT_TLSv4
            || transportConfig.kind == TRANSPORT_DTLSv4
            || (transportConfig.kind == TRANSPORT_WANv4
            && PM::GetInstance().get<bool>("transportSecureWan"))) {
        stringStream << "\tCertificate authority file: "
                     << PM::GetInstance().get<std::string>(
                            "transportCertAuthority") << "\n";
        stringStream << "\tCertificate file: "
                     << PM::GetInstance().get<std::string>("transportCertFile")
                     << "\n";
        stringStream << "\tPrivate key file: "
                     << PM::GetInstance().get<std::string>(
                            "transportPrivateKey") << "\n";
    }

    if (!PM::GetInstance().get<std::string>("transportVerbosity").empty()) {
        stringStream << "\tVerbosity: "
                     << PM::GetInstance().get<std::string>("transportVerbosity")
                     << "\n";
    }

    return stringStream.str();
}

/*********************************************************
 * Validate and manage the parameter
 */
bool PerftestTransport::validate_input()
{
    // TODO: (Alfonso) Manage parameter -multicastAddr
    // } else if (IS_OPTION(argv[i], "-multicastAddr")) {
    //     PM::GetInstance().set("multicast", true);
    //     if ((i == (argc - 1)) || *argv[++i] == '-') {
    //         fprintf(stderr,
    //                 "%s Missing <address> after "
    //                 "-multicastAddr\n",
    //                 classLoggingString.c_str());
    //         return false;
    //     }
    //     multicastAddrMap[THROUGHPUT_TOPIC_NAME] = argv[i];
    //     multicastAddrMap[LATENCY_TOPIC_NAME] = argv[i];
    //     multicastAddrMap[ANNOUNCEMENT_TOPIC_NAME] = argv[i];
    // }

    // Manage parameter -allowInterfaces -nic
    if (PM::GetInstance().get<std::string>("allowInterfaces").empty()) {
        PM::GetInstance().set<std::string>(
                "allowInterfaces",
                PM::GetInstance().get<std::string>("nic"));
    }

    // Manage parameter -transport
    if (!setTransport(PM::GetInstance().get<std::string>(
                "transport"))) {
        fprintf(stderr,
                "%s Error Setting the transport\n",
                classLoggingString.c_str());
        return false;
    }

    // We only need to set the secure properties for this
    if (transportConfig.kind == TRANSPORT_TLSv4
            || transportConfig.kind == TRANSPORT_DTLSv4
            || transportConfig.kind == TRANSPORT_WANv4) {
        populateSecurityFiles();
    }

    return true;
}

bool PerftestTransport::allowsMulticast()
{
    return (transportConfig.kind != TRANSPORT_TCPv4
            && transportConfig.kind != TRANSPORT_TLSv4
            && transportConfig.kind != TRANSPORT_WANv4
            && transportConfig.kind != TRANSPORT_SHMEM);
}

const std::string PerftestTransport::getMulticastAddr(const char *topicName)
{
    std::string address = multicastAddrMap[std::string(topicName)];

    if (address.length() == 0) {
        return NULL;
    }

    return address;
}

/******************************************************************************/
