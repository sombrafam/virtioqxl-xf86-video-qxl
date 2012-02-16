#ifndef OPTION_HELPERS_H
#define OPTION_HELPERS_H


int get_int_option(OptionInfoPtr options, int option_index,
                   const char *env_name);

const char *get_str_option(OptionInfoPtr options, int option_index,
                           const char *env_name);

int get_bool_option(OptionInfoPtr options, int option_index,
                     const char *env_name);

#endif // OPTION_HELPERS_H
