xquery version "3.1";
declare function local:hello($name) {
    concat("Hello, ", $name)
};
