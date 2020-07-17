#include "geofeatures.h"


using namespace godot;


// GeoFeature

GeoFeature::GeoFeature() {

}

GeoFeature::~GeoFeature() {
    delete gdal_feature;
}

void GeoFeature::_init() {
    init_ref();
}

void GeoFeature::_register_methods() {
    register_method("get_attribute", &GeoFeature::get_attribute);
}

String GeoFeature::get_attribute(String name) {
    return gdal_feature->get_attribute(name.utf8().get_data());
}


Array GeoFeature::get_attributes() {
    Array attributes = Array();

    // TODO

    return attributes;
}

void GeoFeature::set_gdal_feature(Feature *gdal_feature) {
    this->gdal_feature = gdal_feature;
}


// GeoPoint

void GeoPoint::_init() {
    init_ref();
}

void GeoPoint::_register_methods() {
    register_method("get_vector3", &GeoPoint::get_vector3);
    register_method("get_offset_vector3", &GeoPoint::get_offset_vector3);
}

Vector3 GeoPoint::get_offset_vector3(int offset_x, int offset_y, int offset_z) {
    PointFeature *point = (PointFeature *) gdal_feature;

    return Vector3(point->get_x() + offset_x, point->get_z() + offset_y, -(point->get_y() + offset_z));
}

Vector3 GeoPoint::get_vector3() {
    return get_offset_vector3(0, 0, 0);
}


// GeoLine

void GeoLine::_init() {
    init_ref();
}

void GeoLine::_register_methods() {
    register_method("get_curve3d", &GeoLine::get_curve3d);
    register_method("get_offset_curve3d", &GeoLine::get_offset_curve3d);
}

Ref<Curve3D> GeoLine::get_offset_curve3d(int offset_x, int offset_y, int offset_z) {
    LineFeature *line = (LineFeature *) gdal_feature;

    Ref<Curve3D> curve = Curve3D::_new();

    int point_count = line->get_point_count();

    for (int i = 0; i < point_count; i++) {
        // Note: y and z are swapped because of differences in the coordinate system!
        double x = line->get_line_point_x(i) + static_cast<double>(offset_x);
        double y = line->get_line_point_z(i) + static_cast<double>(offset_y);
        double z = -(line->get_line_point_y(i) + static_cast<double>(offset_z));

        curve->add_point(Vector3(x, y, z));
    }

    return curve;
}

Ref<Curve3D> GeoLine::get_curve3d() {
    return get_offset_curve3d(0, 0, 0);
}
