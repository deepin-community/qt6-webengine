# -*- coding: utf-8 -*-
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: traffic_annotation.proto
"""Generated protocol buffer code."""
from google.protobuf.internal import builder as _builder
from google.protobuf import descriptor as _descriptor
from google.protobuf import descriptor_pool as _descriptor_pool
from google.protobuf import symbol_database as _symbol_database
# @@protoc_insertion_point(imports)

_sym_db = _symbol_database.Default()


import chrome_settings_pb2 as chrome__settings__pb2
import chrome_device_policy_pb2 as chrome__device__policy__pb2


DESCRIPTOR = _descriptor_pool.Default().AddSerializedFile(b'\n\x18traffic_annotation.proto\x12\x12traffic_annotation\x1a\x15\x63hrome_settings.proto\x1a\x1a\x63hrome_device_policy.proto\"\xfe\x0f\n\x18NetworkTrafficAnnotation\x12\x11\n\tunique_id\x18\x01 \x01(\t\x12J\n\x06source\x18\x02 \x01(\x0b\x32:.traffic_annotation.NetworkTrafficAnnotation.TrafficSource\x12P\n\tsemantics\x18\x03 \x01(\x0b\x32=.traffic_annotation.NetworkTrafficAnnotation.TrafficSemantics\x12J\n\x06policy\x18\x04 \x01(\x0b\x32:.traffic_annotation.NetworkTrafficAnnotation.TrafficPolicy\x12\x10\n\x08\x63omments\x18\x05 \x01(\t\x1a@\n\rTrafficSource\x12\x0c\n\x04\x66ile\x18\x01 \x01(\t\x12\x0c\n\x04line\x18\x03 \x01(\x05\x12\x13\n\x0b\x63\x61ll_number\x18\x04 \x01(\x05\x1a\xe6\t\n\x10TrafficSemantics\x12\x0e\n\x06sender\x18\x01 \x01(\t\x12\x13\n\x0b\x64\x65scription\x18\x02 \x01(\t\x12\x0f\n\x07trigger\x18\x03 \x01(\t\x12\x0c\n\x04\x64\x61ta\x18\x04 \x01(\t\x12^\n\x0b\x64\x65stination\x18\x05 \x01(\x0e\x32I.traffic_annotation.NetworkTrafficAnnotation.TrafficSemantics.Destination\x12\x19\n\x11\x64\x65stination_other\x18\x06 \x01(\t\x12X\n\x08internal\x18\x07 \x01(\x0b\x32\x46.traffic_annotation.NetworkTrafficAnnotation.TrafficSemantics.Internal\x12Y\n\tuser_data\x18\x08 \x01(\x0b\x32\x46.traffic_annotation.NetworkTrafficAnnotation.TrafficSemantics.UserData\x12\x15\n\rlast_reviewed\x18\t \x01(\t\x1a\x98\x01\n\x08Internal\x12`\n\x08\x63ontacts\x18\x01 \x03(\x0b\x32N.traffic_annotation.NetworkTrafficAnnotation.TrafficSemantics.Internal.Contact\x1a*\n\x07\x43ontact\x12\x0f\n\x05\x65mail\x18\x01 \x01(\tH\x00\x42\x0e\n\x0c\x63ontact_type\x1a\xcd\x04\n\x08UserData\x12\x61\n\x04type\x18\x01 \x03(\x0e\x32S.traffic_annotation.NetworkTrafficAnnotation.TrafficSemantics.UserData.UserDataType\"\xdd\x03\n\x0cUserDataType\x12\x0f\n\x0bUNSPECIFIED\x10\x00\x12\x10\n\x0c\x41\x43\x43\x45SS_TOKEN\x10\x01\x12\x0b\n\x07\x41\x44\x44RESS\x10\x02\x12\x0e\n\nANDROID_ID\x10\x03\x12\x07\n\x03\x41GE\x10\x04\x12\x12\n\x0e\x41RBITRARY_DATA\x10\x05\x12\x0e\n\nBIRTH_DATE\x10\x06\x12\x0f\n\x0b\x43REDENTIALS\x10\x07\x12\x14\n\x10\x43REDIT_CARD_DATA\x10\x08\x12\r\n\tDEVICE_ID\x10\t\x12\t\n\x05\x45MAIL\x10\n\x12\r\n\tFILE_DATA\x10\x0b\x12\x0b\n\x07GAIA_ID\x10\x0c\x12\n\n\x06GENDER\x10\r\x12\x11\n\rGOVERNMENT_ID\x10\x0e\x12\t\n\x05IMAGE\x10\x0f\x12\x0e\n\nIP_ADDRESS\x10\x10\x12\x13\n\x0fLOCATION_COARSE\x10\x11\x12\x14\n\x10LOCATION_PRECISE\x10\x12\x12\x08\n\x04NAME\x10\x13\x12\t\n\x05PHONE\x10\x14\x12\x10\n\x0cPROFILE_DATA\x10\x15\x12\x11\n\rSENSITIVE_URL\x10\x16\x12\x0e\n\nSESSION_ID\x10\x17\x12\r\n\tTIMESTAMP\x10\x18\x12\x0e\n\nUSER_AGENT\x10\x19\x12\x10\n\x0cUSER_CONTENT\x10\x1a\x12\x0c\n\x08USERNAME\x10\x1b\x12\n\n\x05OTHER\x10\xe7\x07\x12\t\n\x04NONE\x10\xe8\x07\"\\\n\x0b\x44\x65stination\x12\x0f\n\x0bUNSPECIFIED\x10\x00\x12\x0b\n\x07WEBSITE\x10\x01\x12\x18\n\x14GOOGLE_OWNED_SERVICE\x10\x02\x12\t\n\x05LOCAL\x10\x03\x12\n\n\x05OTHER\x10\xe8\x07\x1a\xa7\x03\n\rTrafficPolicy\x12\x62\n\x0f\x63ookies_allowed\x18\x01 \x01(\x0e\x32I.traffic_annotation.NetworkTrafficAnnotation.TrafficPolicy.CookiesAllowed\x12\x15\n\rcookies_store\x18\x02 \x01(\t\x12\x0f\n\x07setting\x18\x03 \x01(\t\x12\x41\n\rchrome_policy\x18\x04 \x03(\x0b\x32*.enterprise_management.ChromeSettingsProto\x12N\n\x14\x63hrome_device_policy\x18\x07 \x03(\x0b\x32\x30.enterprise_management.ChromeDeviceSettingsProto\x12&\n\x1epolicy_exception_justification\x18\x05 \x01(\t\x12\x1b\n\x13\x64\x65precated_policies\x18\x06 \x03(\t\"2\n\x0e\x43ookiesAllowed\x12\x0f\n\x0bUNSPECIFIED\x10\x00\x12\x06\n\x02NO\x10\x01\x12\x07\n\x03YES\x10\x02\"u\n!ExtractedNetworkTrafficAnnotation\x12P\n\x1anetwork_traffic_annotation\x18\x01 \x03(\x0b\x32,.traffic_annotation.NetworkTrafficAnnotation\"x\n$WhitelistedNetworkTrafficAnnotations\x12P\n\x1anetwork_traffic_annotation\x18\x01 \x03(\x0b\x32,.traffic_annotation.NetworkTrafficAnnotation\"\xec\x01\n\x19NetworkTrafficAnnotations\x12\x64\n%extracted_network_traffic_annotations\x18\x01 \x01(\x0b\x32\x35.traffic_annotation.ExtractedNetworkTrafficAnnotation\x12i\n\'whitelisted_network_traffic_annotations\x18\x02 \x01(\x0b\x32\x38.traffic_annotation.WhitelistedNetworkTrafficAnnotationsB\x02H\x03\x62\x06proto3')

_builder.BuildMessageAndEnumDescriptors(DESCRIPTOR, globals())
_builder.BuildTopDescriptorsAndMessages(DESCRIPTOR, 'traffic_annotation_pb2', globals())
if _descriptor._USE_C_DESCRIPTORS == False:

  DESCRIPTOR._options = None
  DESCRIPTOR._serialized_options = b'H\003'
  _NETWORKTRAFFICANNOTATION._serialized_start=100
  _NETWORKTRAFFICANNOTATION._serialized_end=2146
  _NETWORKTRAFFICANNOTATION_TRAFFICSOURCE._serialized_start=399
  _NETWORKTRAFFICANNOTATION_TRAFFICSOURCE._serialized_end=463
  _NETWORKTRAFFICANNOTATION_TRAFFICSEMANTICS._serialized_start=466
  _NETWORKTRAFFICANNOTATION_TRAFFICSEMANTICS._serialized_end=1720
  _NETWORKTRAFFICANNOTATION_TRAFFICSEMANTICS_INTERNAL._serialized_start=882
  _NETWORKTRAFFICANNOTATION_TRAFFICSEMANTICS_INTERNAL._serialized_end=1034
  _NETWORKTRAFFICANNOTATION_TRAFFICSEMANTICS_INTERNAL_CONTACT._serialized_start=992
  _NETWORKTRAFFICANNOTATION_TRAFFICSEMANTICS_INTERNAL_CONTACT._serialized_end=1034
  _NETWORKTRAFFICANNOTATION_TRAFFICSEMANTICS_USERDATA._serialized_start=1037
  _NETWORKTRAFFICANNOTATION_TRAFFICSEMANTICS_USERDATA._serialized_end=1626
  _NETWORKTRAFFICANNOTATION_TRAFFICSEMANTICS_USERDATA_USERDATATYPE._serialized_start=1149
  _NETWORKTRAFFICANNOTATION_TRAFFICSEMANTICS_USERDATA_USERDATATYPE._serialized_end=1626
  _NETWORKTRAFFICANNOTATION_TRAFFICSEMANTICS_DESTINATION._serialized_start=1628
  _NETWORKTRAFFICANNOTATION_TRAFFICSEMANTICS_DESTINATION._serialized_end=1720
  _NETWORKTRAFFICANNOTATION_TRAFFICPOLICY._serialized_start=1723
  _NETWORKTRAFFICANNOTATION_TRAFFICPOLICY._serialized_end=2146
  _NETWORKTRAFFICANNOTATION_TRAFFICPOLICY_COOKIESALLOWED._serialized_start=2096
  _NETWORKTRAFFICANNOTATION_TRAFFICPOLICY_COOKIESALLOWED._serialized_end=2146
  _EXTRACTEDNETWORKTRAFFICANNOTATION._serialized_start=2148
  _EXTRACTEDNETWORKTRAFFICANNOTATION._serialized_end=2265
  _WHITELISTEDNETWORKTRAFFICANNOTATIONS._serialized_start=2267
  _WHITELISTEDNETWORKTRAFFICANNOTATIONS._serialized_end=2387
  _NETWORKTRAFFICANNOTATIONS._serialized_start=2390
  _NETWORKTRAFFICANNOTATIONS._serialized_end=2626
# @@protoc_insertion_point(module_scope)