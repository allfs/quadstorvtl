<!-- 

function ValidString(str){
	var re = /[^a-zA-Z0-9_\-]/

	if (re.test(str))
		return false;

	return true;
}

function ValidIQNString(str){
	var re = /[^a-zA-Z0-9_\-.]/

	if (re.test(str))
		return false;

	if (str[0] == '.' || str[0] == '_' || str[0] == '-') {
		alert(str[0]);
		return false;
	}

	return true;
}

function checkform()
{
	var frm = document.getElementById('vtiscsiconf');

	if (!ValidIQNString(frm.iqn.value)) {
		alert("IQN User can only contains alphabets or numbers");
		return false;
	}

	if (!ValidString(frm.IncomingUser.value)) {
		alert("Incoming User can only contains alphabets or numbers");
		return false;
	}

	if (!ValidString(frm.IncomingPasswd.value)) {
		alert("Incoming Passwd can only contains alphabets or numbers");
		return false;
	}

	if (!ValidString(frm.OutgoingUser.value)) {
		alert("Outgoing User can only contains alphabets or numbers");
		return false;
	}

	if (!ValidString(frm.OutgoingPasswd.value)) {
		alert("Outgoing Passwd can only contains alphabets or numbers");
		return false;
	}

	return true;

}

// -->
