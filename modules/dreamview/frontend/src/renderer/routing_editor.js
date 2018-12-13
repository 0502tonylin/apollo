import * as THREE from "three";
import "imports-loader?THREE=three!three/examples/js/controls/OrbitControls.js";

import routingPointPin from "assets/images/routing/pin.png";

import STORE from "store";
import WS from "store/websocket";
import { drawImage } from "utils/draw";

export default class RoutingEditor {
    constructor() {
        this.routePoints = [];
        this.parkingSpaceId = null;
        this.inEditingMode = false;
    }

    isInEditingMode() {
        return this.inEditingMode;
    }

    enableEditingMode(camera, adc) {
        this.inEditingMode = true;

        const pov = "Map";
        camera.fov = PARAMETERS.camera[pov].fov;
        camera.near = PARAMETERS.camera[pov].near;
        camera.far = PARAMETERS.camera[pov].far;

        camera.updateProjectionMatrix();
        WS.requestMapElementIdsByRadius(PARAMETERS.routingEditor.radiusOfMapRequest);
    }

    disableEditingMode(scene) {
        this.inEditingMode = false;
        this.removeAllRoutePoints(scene);
        this.parkingSpaceId = null;
    }

    addRoutingPoint(point, coordinates, scene) {
        const offsetPoint = coordinates.applyOffset({x:point.x, y:point.y});
        const pointMesh = drawImage(routingPointPin, 3.5, 3.5, offsetPoint.x, offsetPoint.y, 0.3);
        this.routePoints.push(pointMesh);
        scene.add(pointMesh);
    }

    setParkingSpaceId(id) {
        this.parkingSpaceId = id;
    }

    removeLastRoutingPoint(scene) {
        const lastPoint = this.routePoints.pop();
        if (lastPoint) {
            scene.remove(lastPoint);
            if (lastPoint.geometry) {
                lastPoint.geometry.dispose();
            }
            if (lastPoint.material) {
                lastPoint.material.dispose();
            }
        }
    }

    removeAllRoutePoints(scene) {
        this.routePoints.forEach((object) => {
            scene.remove(object);
            if (object.geometry) {
                object.geometry.dispose();
            }
            if (object.material) {
                object.material.dispose();
            }
        });
        this.routePoints = [];
    }

    sendRoutingRequest(carOffsetPosition, coordinates) {
        if (this.routePoints.length === 0) {
            alert("Please provide at least an end point.");
            return false;
        }

        const points = this.routePoints.map((object) => {
            object.position.z = 0;
            return coordinates.applyOffset(object.position, true);
        });

        const start    = (points.length > 1) ? points[0]
                         : coordinates.applyOffset(carOffsetPosition, true);
        const end      = points[points.length-1];
        const waypoint = (points.length > 1) ? points.slice(1,-1) : [];
        WS.requestRoute(start, waypoint, end, this.parkingSpaceId);

        return true;
    }
}