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

export interface ViscaResponse {
  status: string;
  id: string;
  data: Uint8Array;
}
