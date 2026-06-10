#pragma once

void protocol_init(void);
void protocol_handle_line(const char* line);
void protocol_send_ready_event(void);
