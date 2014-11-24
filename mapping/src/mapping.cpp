#include "mapping/mapping.h"

const int Mapping::GRID_HEIGHT = 1000;
const int Mapping::GRID_WIDTH = 1000;
const int Mapping::GRID_X_OFFSET = GRID_WIDTH/2;
const int Mapping::GRID_Y_OFFSET = GRID_HEIGHT/2;

const double Mapping::MAP_HEIGHT = 10.0;
const double Mapping::MAP_WIDTH = 10.0;
const double Mapping::MAP_X_OFFSET = MAP_WIDTH/2.0;
const double Mapping::MAP_Y_OFFSET = MAP_HEIGHT/2.0;

const double Mapping::P_PRIOR = log(0.5);
const double Mapping::P_OCC = log(0.7);
const double Mapping::P_FREE  = log(0.35);

const double Mapping::FREE_OCCUPIED_THRESHOLD = log(0.5);

const double Mapping::INVALID_READING = -1.0;
const int Mapping::UNKNOWN = -1;
const int Mapping::FREE = 0;
const int Mapping::OCCUPIED = 1;
const int Mapping::BLUE_CUBE = 2;
const int Mapping::RED_SPHERE = 3;

typedef pcl::PointCloud<pcl::PointXYZI> PointCloud;
typedef pcl::PointXYZI PCPoint;

Mapping::Mapping() :
    fl_side(INVALID_READING), fr_side(INVALID_READING),
    bl_side(INVALID_READING), br_side(INVALID_READING),
    l_front(INVALID_READING), r_front(INVALID_READING)
{
    handle = ros::NodeHandle("");
    distance_sub = handle.subscribe("/perception/ir/distance", 1000, &Mapping::distanceCallback, this);
    odometry_sub = handle.subscribe("/pose/odometry/", 1000, &Mapping::odometryCallback, this);

    pc_pub = handle.advertise<PointCloud>("/mapping/point_cloud", 1);

    initProbGrid();
    initOccGrid();
    broadcastTransform();
}

void Mapping::broadcastTransform()
{
    tf_broadcaster.sendTransform(
                tf::StampedTransform(
                    tf::Transform(tf::Quaternion(0, 0, 0, 1), tf::Vector3(MAP_X_OFFSET, MAP_Y_OFFSET, 0.0)),
                    ros::Time::now(),"map", "robot"));
}

Point2D<int> Mapping::posToCell(Point2D<double> pos)
{
    int x = round(pos.x/100.0) + GRID_X_OFFSET;
    int y = round(pos.y/100.0) + GRID_Y_OFFSET;
    return Point2D<int>(x,y);
}

void Mapping::updateGrid()
{
    if(isReadingValid(fl_side))
        updateFL();

    if(isReadingValid(fr_side))
        updateFR();

    if(isReadingValid(fr_side))
        updateBL();

    if(isReadingValid(br_side))
        updateBR();

    // TODO update cells robot is at to free
}

bool Mapping::isReadingValid(double value)
{
    return value > 0.0;
}

void Mapping::updateFL()
{
    Point2D<double> fl_point = getPointPos(-1*robot::ir::offset_front_left, robot::ir::offset_front_left_forward, fl_side);
    Point2D<double> fl_sensor = getPointPos(-1*robot::ir::offset_front_left, robot::ir::offset_front_left_forward, 0.0);
    updateProbCell(posToCell(fl_point), P_OCC);
    updateOccCell(posToCell(fl_point));
    updateFreeCells(fl_sensor, fl_point);
}

void Mapping::updateFR()
{
    Point2D<double> fr_point = getPointPos(robot::ir::offset_front_right, robot::ir::offset_front_right_forward, fr_side);
    Point2D<double> fr_sensor = getPointPos(robot::ir::offset_front_right, robot::ir::offset_front_right_forward, 0.0);
    updateProbCell(posToCell(fr_point), P_OCC);
    updateOccCell(posToCell(fr_point));
    updateFreeCells(fr_sensor, fr_point);
}

void Mapping::updateBR()
{
    Point2D<double> br_point = getPointPos(robot::ir::offset_rear_right, -1*robot::ir::offset_rear_right_forward, br_side);
    Point2D<double> br_sensor = getPointPos(robot::ir::offset_rear_right, -1*robot::ir::offset_rear_right_forward, 0.0);
    updateProbCell(posToCell(br_point), P_OCC);
    updateOccCell(posToCell(br_point));
    updateFreeCells(br_sensor, br_point);
}

void Mapping::updateBL()
{
    Point2D<double> bl_point = getPointPos(-1*robot::ir::offset_rear_left, -1*robot::ir::offset_rear_left_forward, bl_side);
    Point2D<double> bl_sensor = getPointPos(-1*robot::ir::offset_rear_left, -1*robot::ir::offset_rear_left_forward, 0.0);
    updateProbCell(posToCell(bl_point), P_OCC);
    updateOccCell(posToCell(bl_point));
    updateFreeCells(bl_sensor, bl_point);
}

Point2D<double> Mapping::getPointPos(double side_offset, double forward_offset, double ir_distance)
{
    geometry_msgs::PointStamped robot_point;
    robot_point.header.frame_id = "robot";
    robot_point.header.stamp = ros::Time();
    robot_point.point.x = pos.x + side_offset + ir_distance;
    robot_point.point.y = pos.y + forward_offset;
    robot_point.point.z = 0.0;

    geometry_msgs::PointStamped map_point;
    tf_listener.transformPoint("map", robot_point, map_point);

    return Point2D<double>(map_point.point.x, map_point.point.y);
}

void Mapping::updateFreeCells(Point2D<double> sensor, Point2D<double> obstacle)
{
    double k = (sensor.y - obstacle.y)/(sensor.x - obstacle.x);
    double m = sensor.y - k*sensor.x;

    double min_x = std::min(sensor.x, obstacle.x) + 1.0;
    double max_x = std::max(sensor.x, obstacle.x);
    double min_y = std::min(sensor.y, obstacle.y) + 1.0;
    double max_y = std::max(sensor.y, obstacle.y);

    for(double x = min_x; x < max_x; ++x)
    {
        for(double y = min_y; y < max_y; ++y)
        {
            Point2D<int> cell = posToCell(Point2D<double>(x,y));
            updateProbCell(cell, P_FREE);
            updateOccCell(cell);
        }
    }
}

void Mapping::updateProbCell(Point2D<int> cell, double p)
{
    prob_grid[cell.x][cell.y] += p - P_PRIOR;
}

void Mapping::updateOccCell(Point2D<int> cell)
{
    if(prob_grid[cell.x][cell.y] > FREE_OCCUPIED_THRESHOLD)
        occ_grid[cell.x][cell.y] = OCCUPIED;
    if(prob_grid[cell.x][cell.y] < FREE_OCCUPIED_THRESHOLD)
        occ_grid[cell.x][cell.y] = FREE;
    occ_grid[cell.x][cell.y] = UNKNOWN;

}

void Mapping::initProbGrid()
{
    prob_grid.resize(GRID_WIDTH);
    for(int x = 0; x < GRID_WIDTH; ++x)
        prob_grid[x].resize(GRID_HEIGHT);

    for(int x = 0; x < GRID_WIDTH; ++x)
        for(int y = 0; y < GRID_HEIGHT; ++y)
            prob_grid[x][y] = P_PRIOR;
}

void Mapping::initOccGrid()
{
    occ_grid.resize(GRID_WIDTH);
    for(int x = 0; x < GRID_WIDTH; ++x)
        occ_grid[x].resize(GRID_HEIGHT);

    for(int x = 0; x < GRID_WIDTH; ++x)
        for(int y = 0; y < GRID_HEIGHT; ++y)
            occ_grid[x][y] = UNKNOWN;
}

void Mapping::distanceCallback(const ir_converter::Distance::ConstPtr& distance)
{
    fl_side = distance->fl_side;
    bl_side = distance->bl_side;
    fr_side = distance->fr_side;
    br_side = distance->br_side;
    l_front = distance->l_front;
    r_front = distance->r_front;
}

void Mapping::odometryCallback(const nav_msgs::Odometry::ConstPtr& odom)
{
    double x = odom->pose.pose.position.x;
    double y = odom->pose.pose.position.y;
    pos = Point2D<double>(x,y);
}

void Mapping::publishMap()
{
    PointCloud::Ptr msg (new PointCloud);
    msg->header.frame_id = "map";
    msg->height = GRID_HEIGHT;
    msg->width = GRID_WIDTH;

    for(int x = 0; x < GRID_WIDTH; ++x)
    {
        for(int y = 0; y < GRID_HEIGHT; ++y)
        {
            int cell = occ_grid[x][y];
            double i = (cell == FREE) ? 0.0 : (cell == OCCUPIED) ? 1.0 : 0.5;

            PCPoint p(i);
            p.x = (double) x/100.0;
            p.y = (double) y/100.0;
            msg->points.push_back(p);
        }
    }
    pc_pub.publish(msg);
}

int main(int argc, char **argv)
{
    ros::init(argc, argv, "mapping");
    Mapping mapping;
    ros::Rate loop_rate(10); // what should this be?
    int counter = 0;
    while(ros::ok())
    {
        ++counter;
        mapping.broadcastTransform();
        mapping.updateGrid();
        if(counter % 100 == 0)
            mapping.publishMap();
        ros::spinOnce();
        loop_rate.sleep();
    }
}