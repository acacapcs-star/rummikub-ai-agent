#include "turn_metrics.h"
int  TurnMetrics::turn_index = 0;
bool TurnMetrics::regroup_attempted = false;
int  TurnMetrics::tiles_played = 0;
bool TurnMetrics::melded = false;
int  TurnMetrics::meld_attempts = 0;
int  TurnMetrics::extend_calls = 0;
int  TurnMetrics::failed_applies = 0;
bool TurnMetrics::had_option = false;
