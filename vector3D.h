#define PI 3.14159265358979323

struct vectorC3D
	{
	long double x;
	long double y;
	long double z;
	};

struct vectorP3D
	{
	long double x;
	long double y;
	long double l;
	};

void vectorC3D_assign(struct vectorC3D *t, struct vectorC3D f)
	{
	if (t!=NULL)
		{
		t->x = f.x;
		t->y = f.y;
		t->z = f.z;
		}
	return;
	}

void vectorC3D_normalize(struct vectorC3D *v, long double len)
	{
	long double temp = sqrtl(powl(v->x,2.0)+powl(v->y,2.0)+powl(v->z,2.0));
	if (temp != 0.0)
		{
		v->x = v->x*len/temp;
		v->y = v->y*len/temp;
		v->z = v->z*len/temp;
		}
	return;
	}

void vectorC3D_rotateX(struct vectorC3D *v, long double a)
	{
	long double angle, l = sqrtl(powl(v->y,2.0)+powl(v->z,2.0));
	if (l == 0.0) return;
	//find angle
	if (v->z == 0.0)
		{
		if (v->y > 0.0)
			angle = 90.0;
		else
			angle = 270.0;
		}
	else
		{
		angle = atanl(v->y/v->z)*180.0/PI;
		if (v->z < 0.0) angle += 180.0;
		if (angle < 0.0) angle += 360.0;
		}
	//apply change (assuming input is measured clockwise)
	angle -= a;
	//convert back from new angle
	v->y = sinl(angle*PI/180.0)*l;
	v->z = cosl(angle*PI/180.0)*l;
	return;
	}
void vectorC3D_rotateY(struct vectorC3D *v, long double a)
	{
	long double angle, l = sqrtl(powl(v->x,2.0)+powl(v->z,2.0));
	if (l == 0.0) return;
	//find angle
	if (v->x == 0.0)
		{
		if (v->z > 0.0)
			angle = 90.0;
		else
			angle = 270.0;
		}
	else
		{
		angle = atanl(v->z/v->x)*180.0/PI;
		if (v->x < 0.0) angle += 180.0;
		if (angle < 0.0) angle += 360.0;
		}
	//apply change (assuming input is measured clockwise)
	angle -= a;
	//convert back from new angle
	v->z = sinl(angle*PI/180.0)*l;
	v->x = cosl(angle*PI/180.0)*l;
	return;
	}
void vectorC3D_rotateZ(struct vectorC3D *v, long double a)
	{
	long double angle, l = sqrtl(powl(v->x,2.0)+powl(v->y,2.0));
	if (l == 0.0) return;
	//find angle
	if (v->x == 0.0)
		{
		if (v->y > 0.0)
			angle = 90.0;
		else
			angle = 270.0;
		}
	else
		{
		angle = atanl(v->y/(-v->x))*180.0/PI;
		if (v->x > 0.0) angle += 180.0;
		if (angle < 0.0) angle += 360.0;
		}
	//apply change (assuming input is measured clockwise)
	angle -= a;
	//convert back from new angle
	v->y =  sinl(angle*PI/180.0)*l;
	v->x = -cosl(angle*PI/180.0)*l;
	return;
	}

struct vectorP3D vectorC3D_polar(struct vectorC3D c)
	{
	struct vectorP3D p;
	long double l = sqrtl(powl(c.x,2.0)+powl(c.z,2.0));
	if (l == 0.0)
		{
		if (c.y > 0.0) p.x = 90.0;
		else if (c.y < 0.0) p.x = -90.0;
		else p.x = 0.0;
		}
	else
		p.x = atanl(c.y/l)*180.0/PI;
	p.l = sqrtl(powl(c.x,2.0)+powl(c.y,2.0)+powl(c.z,2.0));
	if (c.x == 0.0)
		{
		if (c.z > 0.0) p.y = 90.0;
		else if (c.z < 0.0) p.y = 270.0;
		else p.y = 0.0;
		}
	else
		{
		p.y = atanl(c.z/c.x)*180.0/PI;
		if (c.x < 0.0) p.y += 180.0;
		if (p.y < 0.0) p.y += 360.0;
		}
	return p;
	}

void vectorP3D_assign(struct vectorP3D *t, struct vectorP3D f)
	{
	if (t!=NULL)
		{
		t->x = f.x;
		t->y = f.y;
		t->l = f.l;
		}
	return;
	}

struct vectorC3D vectorP3D_cartesian(struct vectorP3D p)
	{
	struct vectorC3D c;
	long double l = cosl(p.x*PI/180.0)*p.l;
	c.x = cosl(p.y*PI/180.0)*l;
	c.y = sinl(p.x*PI/180.0)*p.l;
	c.z = sinl(p.y*PI/180.0)*l;
	return c;
	}

