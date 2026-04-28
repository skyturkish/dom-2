#include <json-c/json.h>
#include <stdio.h>
 
void json_parse(json_object *jobj) {
    enum json_type type;
    json_object_object_foreach(jobj, key, val) {
    type = json_object_get_type(val);
    switch (type) {
        case json_type_string:
            printf("type: json_type_string, ");
            printf("value: %s\n", json_object_get_string(val));
            break;
        case json_type_int:
            printf("type: json_type_int, ");
            printf("value: %d\n", json_object_get_int(val));
            break;        
        case json_type_boolean:
            printf("type: json_type_boolean, ");
            printf("value: %d\n", json_object_get_boolean(val));
            break;
        case json_type_double:
            printf("type: json_type_double, ");
            printf("value: %f\n", json_object_get_double(val));
            break;
        case json_type_array:
            printf("type: json_type_array, ");
            jobj = json_object_object_get(jobj, key);
            int arraylen = json_object_array_length(jobj);
            printf("Array Length: %d\n",arraylen);
            int i;
            json_object * jvalue;
            for (i=0; i< arraylen; i++){
                jvalue = json_object_array_get_idx(jobj, i);
                if(i == 0)
                    printf("Min value: %d ", json_object_get_int(jvalue));
                else
                    printf("/ Max value: %d\n", json_object_get_int(jvalue));
            }
            break;
        }
    }   
}

int main() {
    char * string = "{ \"Course\" : \" CME2202\", \"semester\" : 6, \"Prerequisite\" : false, \"averageGrade\" : 68.5, \"Min_Max_Grade\" : [20, 95]}";
    printf ("JSON string: %s\n", string);
    json_object * jobj = json_tokener_parse(string);
    json_parse(jobj);
    return 0;
}
