from __future__ import annotations

from .ai_log_read import SPEC as AI_LOG_READ_SPEC, run as ai_log_read_run
from .browser_fetch_text import SPEC as BROWSER_FETCH_TEXT_SPEC, run as browser_fetch_text_run
from .browser_open import SPEC as BROWSER_OPEN_SPEC, run as browser_open_run
from .coding_generate import SPEC as CODING_GENERATE_SPEC, run as coding_generate_run
from .computer_list_apps import SPEC as COMPUTER_LIST_APPS_SPEC, run as computer_list_apps_run
from .computer_open_app import SPEC as COMPUTER_OPEN_APP_SPEC, run as computer_open_app_run
from .computer_open_url import SPEC as COMPUTER_OPEN_URL_SPEC, run as computer_open_url_run
from .computer_write_file import SPEC as COMPUTER_WRITE_FILE_SPEC, run as computer_write_file_run
from .dir_list import SPEC as DIR_LIST_SPEC, run as dir_list_run
from .file_patch import SPEC as FILE_PATCH_SPEC, run as file_patch_run
from .file_read import SPEC as FILE_READ_SPEC, run as file_read_run
from .file_search import SPEC as FILE_SEARCH_SPEC, run as file_search_run
from .file_write import SPEC as FILE_WRITE_SPEC, run as file_write_run
from .log_search import SPEC as LOG_SEARCH_SPEC, run as log_search_run
from .log_tail import SPEC as LOG_TAIL_SPEC, run as log_tail_run
from .shell_run import SPEC as SHELL_RUN_SPEC, run as shell_run_run
from .weather_current import SPEC as WEATHER_CURRENT_SPEC, run as weather_current_run
from .web_fetch import SPEC as WEB_FETCH_SPEC, run as web_fetch_run
from .web_search import SPEC as WEB_SEARCH_SPEC, run as web_search_run
from .youtube_transcript import SPEC as YOUTUBE_TRANSCRIPT_SPEC, run as youtube_transcript_run

ACTIONS = {
    DIR_LIST_SPEC["name"]: {"spec": DIR_LIST_SPEC, "run": dir_list_run},
    FILE_READ_SPEC["name"]: {"spec": FILE_READ_SPEC, "run": file_read_run},
    FILE_SEARCH_SPEC["name"]: {"spec": FILE_SEARCH_SPEC, "run": file_search_run},
    FILE_WRITE_SPEC["name"]: {"spec": FILE_WRITE_SPEC, "run": file_write_run},
    FILE_PATCH_SPEC["name"]: {"spec": FILE_PATCH_SPEC, "run": file_patch_run},
    LOG_TAIL_SPEC["name"]: {"spec": LOG_TAIL_SPEC, "run": log_tail_run},
    LOG_SEARCH_SPEC["name"]: {"spec": LOG_SEARCH_SPEC, "run": log_search_run},
    AI_LOG_READ_SPEC["name"]: {"spec": AI_LOG_READ_SPEC, "run": ai_log_read_run},
    WEB_FETCH_SPEC["name"]: {"spec": WEB_FETCH_SPEC, "run": web_fetch_run},
    WEB_SEARCH_SPEC["name"]: {"spec": WEB_SEARCH_SPEC, "run": web_search_run},
    COMPUTER_OPEN_URL_SPEC["name"]: {"spec": COMPUTER_OPEN_URL_SPEC, "run": computer_open_url_run},
    COMPUTER_WRITE_FILE_SPEC["name"]: {"spec": COMPUTER_WRITE_FILE_SPEC, "run": computer_write_file_run},
    COMPUTER_LIST_APPS_SPEC["name"]: {"spec": COMPUTER_LIST_APPS_SPEC, "run": computer_list_apps_run},
    COMPUTER_OPEN_APP_SPEC["name"]: {"spec": COMPUTER_OPEN_APP_SPEC, "run": computer_open_app_run},
    SHELL_RUN_SPEC["name"]: {"spec": SHELL_RUN_SPEC, "run": shell_run_run},
    WEATHER_CURRENT_SPEC["name"]: {"spec": WEATHER_CURRENT_SPEC, "run": weather_current_run},
    YOUTUBE_TRANSCRIPT_SPEC["name"]: {"spec": YOUTUBE_TRANSCRIPT_SPEC, "run": youtube_transcript_run},
    BROWSER_OPEN_SPEC["name"]: {"spec": BROWSER_OPEN_SPEC, "run": browser_open_run},
    BROWSER_FETCH_TEXT_SPEC["name"]: {"spec": BROWSER_FETCH_TEXT_SPEC, "run": browser_fetch_text_run},
    CODING_GENERATE_SPEC["name"]: {"spec": CODING_GENERATE_SPEC, "run": coding_generate_run},
}
