export interface ViscaMessageProps {
  ip: string;
  port: number;
  data: Uint8Array;
}

interface ViscaSendMessageProps extends ViscaMessageProps {
  id: string;
}

export interface ViscaMessage {
  op: 'send-visca-cmd';
  props: ViscaSendMessageProps;
}

// export interface ViscaBaseResponse {
//   status: string; // OK if all is well
//   msg?: string;
// }

// export interface ViscaResponse extends ViscaBaseResponse {
//   id: string;
//   data: Uint8Array;
// }

export interface ViscaResponse {
  status: string; // OK if all is well
  msg?: string;
  id?: string;
  data?: Uint8Array;
}
